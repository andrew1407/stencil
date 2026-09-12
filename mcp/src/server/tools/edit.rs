//! `stencil_edit`: one transform of one image/video to one file, then delivery.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use serde::Serialize;

use super::{err_result, ok_result};
use crate::args::EditParams;
use crate::config::Config;
use crate::deliver::{self, DeliveryNote};
use crate::outcome::Remote;
use crate::pipeline;

/// The `stencil_edit` structured payload, serialized as the tool's JSON content. Borrows the
/// result/notes, so `DeliveryNote`/`Remote`'s own `Serialize` shapes the nested objects.
#[derive(Serialize)]
struct EditPayload<'a> {
    path: &'a str,
    width: u32,
    height: u32,
    surfaces: Vec<&'static str>,
    deliveries: &'a [DeliveryNote],
    server: &'a [Remote],
}

/// The tool's whole body: run the CLI, deliver to the resolved surfaces, then report.
pub async fn run(config: &Config, params: EditParams) -> Result<CallToolResult, McpError> {
        let result = match pipeline::run_edit(&params).await {
            Ok(result) => result,
            Err(error) => return Ok(err_result(error.to_string())),
        };

        let surfaces = match params.resolve_surfaces(&config.default_surfaces) {
            Ok(surfaces) => surfaces,
            Err(message) => return Ok(err_result(message)),
        };

        let notes = deliver::deliver(&surfaces, &result, config).await;

        // Summary: the write line + server deliveries, then one line per surface beyond cli.
        use std::fmt::Write;
        let mut summary = result.summary();
        for note in &notes {
            if note.surface == "cli" {
                continue;
            }
            let mark = if note.ok { "→" } else { "✗" };
            let _ = write!(summary, "\n{mark} {}: {}", note.surface, note.detail);
            if let Some(url) = &note.url {
                let _ = write!(summary, "\n  {url}");
            }
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
        };

        ok_result(summary, payload)
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
            width: 800,
            height: 600,
            surfaces: vec!["browser"],
            deliveries: &deliveries,
            server: &server,
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

    #[test]
    fn edit_payload_with_no_deliveries_still_sends_empty_arrays() {
        let value = serde_json::to_value(EditPayload {
            path: "o.png",
            width: 1,
            height: 1,
            surfaces: vec![],
            deliveries: &[],
            server: &[],
        })
        .unwrap();
        // A client iterating these must not have to null-check them.
        assert_eq!(value["surfaces"], json!([]));
        assert_eq!(value["deliveries"], json!([]));
        assert_eq!(value["server"], json!([]));
    }
}
