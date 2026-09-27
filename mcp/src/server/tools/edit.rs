//! `stencil_edit`: one transform of one image/video to one file, then delivery.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use schemars::JsonSchema;
use serde::Serialize;

use super::preview::{PreviewNote, Previews};
use super::{err_result, ok_result};
use crate::args::{self, EditParams, LayoutArg};
use crate::config::{Config, Servers};
use crate::confine::Roots;
use crate::deliver::{self, DeliveryNote};
use crate::outcome::Remote;
use crate::pipeline::{self, EditResult};

/// The `stencil_edit` structured payload, serialized as the tool's JSON content. Borrows the
/// result/notes, so `DeliveryNote`/`Remote`'s own `Serialize` shapes the nested objects.
/// `width`/`height` are null for a `.stencil` project; `preview` only when one is attached.
#[derive(Serialize, JsonSchema)]
pub struct EditPayload<'a> {
    path: &'a str,
    width: Option<u32>,
    height: Option<u32>,
    surfaces: Vec<&'static str>,
    deliveries: &'a [DeliveryNote],
    server: &'a [Remote],
    #[serde(skip_serializing_if = "Option::is_none")]
    preview: Option<&'a PreviewNote>,
}

/// The tool's whole body: fence the call, run the CLI, deliver to the surfaces, report.
pub async fn run(
    config: &Config,
    roots: &Roots,
    mut params: EditParams,
) -> Result<CallToolResult, McpError> {
    if let Err(message) = prepare(config, roots, &mut params) {
        return Ok(err_result(message));
    }
    let result = match pipeline::run_edit(&params).await {
        Ok(result) => result,
        Err(error) => return Ok(err_result(error.to_string())),
    };

    let surfaces = match params.resolve_surfaces(&config.default_surfaces) {
        Ok(surfaces) => surfaces,
        Err(message) => return Ok(err_result(message)),
    };

    let notes = deliver::deliver(&surfaces, &result, config).await;
    let previews = preview(&params, &result).await;

    // Summary: the write line + server deliveries, then one line per surface beyond cli. A
    // launch URL stays in the payload: it can carry the whole image.
    use std::fmt::Write;
    let mut summary = result.summary();
    for note in notes.iter().filter(|n| n.surface != "cli") {
        let mark = if note.ok { "→" } else { "✗" };
        let _ = write!(summary, "\n{mark} {}: {}", note.surface, note.detail);
        if note.url.is_some() {
            summary.push_str(" (launch URL in deliveries[].url)");
        }
    }
    for line in previews.iter().flat_map(|p| &p.lines) {
        let _ = write!(summary, "\n{line}");
    }

    // `server` may touch more than one collaboration server in one call (fetch/update
    // one and create on another), each tagged with its action by `Remote`'s Serialize.
    let payload = EditPayload {
        path: &result.path,
        width: result.width,
        height: result.height,
        surfaces: surfaces.iter().map(|s| s.as_str()).collect(),
        deliveries: &notes,
        server: &result.remotes,
        preview: previews.as_ref().and_then(|p| p.notes.first()),
    };

    let tool_result = ok_result(summary, payload)?;
    Ok(match previews {
        Some(previews) => previews.attach(tool_result),
        None => tool_result,
    })
}

/// The opt-in preview of the written image; a `.stencil` project has none to show.
async fn preview(params: &EditParams, result: &EditResult) -> Option<Previews> {
    let root = params.confine_root.as_deref().filter(|_| params.preview)?;
    match result.width {
        Some(_) => Some(Previews::render(&[&result.path], root).await),
        None => Some(Previews::line("preview: none for a .stencil project".to_string())),
    }
}

/// Fence the call: validate the parameters, anchor every local path on the roots, place the
/// output inside one, and swap server URLs for allowlisted origins plus the operator tokens.
fn prepare(config: &Config, roots: &Roots, params: &mut EditParams) -> Result<(), String> {
    args::build_argv(params, None).map_err(|e| e.to_string())?;
    let (root, output) = roots.place("output", &params.output)?;
    params.output = output.to_string_lossy().into_owned();
    params.confine_root = Some(root.to_string_lossy().into_owned());
    // With `server`, `input` is a project NAME, not a path.
    if let (None, Some(input)) = (&params.server, &params.input) {
        params.input = Some(roots.resolve(input));
    }
    if let Some(LayoutArg::Path(path)) = &params.layout {
        if !path.trim_start().starts_with('{') {
            params.layout = Some(LayoutArg::Path(roots.resolve(path)));
        }
    }
    connect(&config.servers, params)
}

/// Only allowlisted servers, each named by its canonical origin. Each one's operator token
/// rides the child's environment, where the CLI matches it to that origin — never argv.
fn connect(servers: &Servers, params: &mut EditParams) -> Result<(), String> {
    let server = params.server.as_deref().map(|url| servers.find("server", url)).transpose()?;
    let remote = params.remote.as_deref().map(|url| servers.find("remote", url)).transpose()?;
    params.server_tokens = Servers::child_tokens(server.into_iter().chain(remote));
    params.server = server.map(|entry| entry.origin.clone());
    params.remote = remote.map(|entry| entry.origin.clone());
    Ok(())
}

#[cfg(test)]
mod tests {
    //! The edit payload shape (pure) — the contract with a calling agent.

    use super::*;
    use crate::outcome::Remote;
    use serde_json::json;

    #[test]
    fn edit_payload_shape() {
        let deliveries = vec![DeliveryNote {
            surface: "browser",
            ok: true,
            detail: "opened".into(),
            url: Some("http://localhost:8080/#stencil=…".into()),
        }];
        let server = vec![Remote::Created {
            name: "Shot".into(),
            id: "p_1".into(),
        }];
        let value = serde_json::to_value(EditPayload {
            path: "/tmp/out.png",
            width: Some(800),
            height: Some(600),
            surfaces: vec!["browser"],
            deliveries: &deliveries,
            server: &server,
            preview: None,
        })
        .unwrap();

        assert_eq!(value["path"], "/tmp/out.png");
        assert_eq!(value["width"], 800);
        assert_eq!(value["height"], 600);
        assert_eq!(value["surfaces"], json!(["browser"]));
        assert_eq!(value["deliveries"][0]["surface"], "browser");
        assert_eq!(value["deliveries"][0]["ok"], true);
        assert_eq!(value["deliveries"][0]["url"], "http://localhost:8080/#stencil=…");
        // Remote's own Serialize tags the action — the handler never hand-builds this.
        assert_eq!(value["server"][0]["action"], "created");
        assert_eq!(value["server"][0]["id"], "p_1");
    }

    fn connected(server: Option<&str>, remote: Option<&str>) -> Result<EditParams, String> {
        let servers = Servers::parse(
            Some("http://localhost:8090, http://a.test, http://b.test"),
            Some("http://localhost:8090=local, http://a.test=shared, http://b.test=shared"),
            &mut Vec::new(),
        );
        let mut params: EditParams = serde_json::from_value(json!({
            "input": "Plans", "server": server, "remote": remote, "output": "p.png",
        }))
        .unwrap();
        connect(&servers, &mut params).map(|()| params)
    }

    /// A server URL becomes its allowlisted origin, with the operator's token for it.
    #[test]
    fn a_server_rides_as_its_origin_with_the_operator_token() {
        let p = connected(Some("localhost:8090/projects#token=model"), None).unwrap();
        assert_eq!(p.server.as_deref(), Some("http://localhost:8090"));
        assert_eq!(p.server_tokens.as_deref(), Some("http://localhost:8090=local"));
    }

    /// Each origin carries its own token, so one run fetches from one server and publishes
    /// to another whatever their tokens are.
    #[test]
    fn two_origins_each_carry_their_own_token() {
        let p = connected(Some("http://localhost:8090"), Some("http://a.test")).unwrap();
        let tokens = p.server_tokens.as_deref();
        assert_eq!(tokens, Some("http://localhost:8090=local,http://a.test=shared"));
        let same = connected(Some("http://a.test"), Some("http://a.test")).unwrap();
        assert_eq!(same.server_tokens.as_deref(), Some("http://a.test=shared"));
        assert!(connected(None, Some("http://c.test")).is_err(), "c.test is not allowlisted");
        assert_eq!(connected(None, None).unwrap().server_tokens, None);
    }

    #[test]
    fn edit_payload_with_no_deliveries_still_sends_empty_arrays() {
        let value = serde_json::to_value(EditPayload {
            path: "o.png",
            width: Some(1),
            height: Some(1),
            surfaces: vec![],
            deliveries: &[],
            server: &[],
            preview: None,
        })
        .unwrap();
        // A client iterating these must not have to null-check them.
        assert_eq!(value["surfaces"], json!([]));
        assert_eq!(value["deliveries"], json!([]));
        assert_eq!(value["server"], json!([]));
        assert!(value.get("preview").is_none(), "no key unless a preview is attached");
    }
}
