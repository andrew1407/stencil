//! The MCP surface: a server exposing `stencil_edit` and `stencil_probe` over stdio.
//! The `stencil_prompt` flow (LLM turn → op-plan → CLI runs) lives in [`prompt`].

mod prompt;

pub use prompt::run_prompt;

use rmcp::handler::server::router::tool::ToolRouter;
use rmcp::handler::server::wrapper::Parameters;
use rmcp::model::{
    CallToolResult, Content, Implementation, ProtocolVersion, ServerCapabilities, ServerInfo,
};
use rmcp::{tool, tool_handler, tool_router, ErrorData as McpError, ServerHandler};
use serde::Serialize;

use crate::args::{EditParams, ProbeParams, PromptParams, ScrapeParams};
use crate::config::Config;
use crate::deliver;
use crate::deliver::DeliveryNote;
use crate::llmtransport::PlainHttpTransport;
use crate::outcome::{Remote, ScrapedFile};
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

/// The `source_site` structured payload: the destination directory, the scraped page's host,
/// and every downloaded file — `ScrapedFile`'s `Serialize` shapes each `{path,width,height}`.
#[derive(Serialize)]
struct ScrapePayload<'a> {
    dir: Option<&'a str>,
    host: Option<&'a str>,
    files: &'a [ScrapedFile],
}

/// This surface's user-facing prose, embedded from the committed canonical asset. rmcp's
/// `#[tool]` takes a literal, so the four descriptions ride in on `#[doc = include_str!]`
/// from its generated `toolDescriptions/*.txt` shards; `tests/tool_prose_test.rs` pins
/// those and README.md's Tools table against this file.
static PROSE: std::sync::LazyLock<serde_json::Value> = std::sync::LazyLock::new(|| {
    serde_json::from_str(include_str!("../../toolDescriptions.json"))
        .expect("mcp/toolDescriptions.json is not valid JSON")
});

/// Wrap a text summary + JSON payload as a successful tool result.
fn ok_result(summary: String, payload: impl Serialize) -> Result<CallToolResult, McpError> {
    Ok(CallToolResult::success(vec![
        Content::text(summary),
        Content::json(payload)?,
    ]))
}

/// Wrap a message as a tool error result.
fn err_result(message: String) -> CallToolResult {
    CallToolResult::error(vec![Content::text(message)])
}

/// The Stencil MCP server. Cloneable so the transport can share it across requests; its only
/// state is the resolved configuration and the generated tool router.
#[derive(Clone)]
pub struct StencilServer {
    config: Config,
    // Read by the `#[tool_handler]`-generated dispatch; the dead-code lint misses that use.
    #[allow(dead_code)]
    tool_router: ToolRouter<StencilServer>,
}

impl Default for StencilServer {
    fn default() -> Self {
        Self::new(Config::default())
    }
}

#[tool_router]
impl StencilServer {
    pub fn new(config: Config) -> Self {
        Self {
            config,
            tool_router: Self::tool_router(),
        }
    }

    #[doc = include_str!("../../toolDescriptions/stencil_edit.txt")]
    #[tool]
    async fn stencil_edit(
        &self,
        Parameters(params): Parameters<EditParams>,
    ) -> Result<CallToolResult, McpError> {
        let result = match pipeline::run_edit(&params).await {
            Ok(result) => result,
            Err(error) => return Ok(err_result(error.to_string())),
        };

        let surfaces = match params.resolve_surfaces(&self.config.default_surfaces) {
            Ok(surfaces) => surfaces,
            Err(message) => return Ok(err_result(message)),
        };

        let notes = deliver::deliver(&surfaces, &result, &self.config).await;

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

    #[doc = include_str!("../../toolDescriptions/stencil_probe.txt")]
    #[tool]
    async fn stencil_probe(
        &self,
        Parameters(params): Parameters<ProbeParams>,
    ) -> Result<CallToolResult, McpError> {
        match pipeline::run_probe(&params.input).await {
            Ok((width, height)) => {
                let summary = format!("{width}x{height}");
                let payload = serde_json::json!({ "width": width, "height": height });
                ok_result(summary, payload)
            }
            Err(message) => Ok(err_result(message)),
        }
    }

    #[doc = include_str!("../../toolDescriptions/stencil_prompt.txt")]
    #[tool]
    async fn stencil_prompt(
        &self,
        Parameters(params): Parameters<PromptParams>,
    ) -> Result<CallToolResult, McpError> {
        run_prompt(
            &self.config,
            std::sync::Arc::new(PlainHttpTransport::new()),
            params,
        )
        .await
    }

    #[doc = include_str!("../../toolDescriptions/source_site.txt")]
    #[tool]
    async fn source_site(
        &self,
        Parameters(params): Parameters<ScrapeParams>,
    ) -> Result<CallToolResult, McpError> {
        // Scraping only writes files locally: reject any other surface before the CLI runs.
        if let Err(message) = params.validate_surface() {
            return Ok(err_result(message));
        }

        let result = match pipeline::run_scrape(&params).await {
            Ok(result) => result,
            Err(error) => return Ok(err_result(error.to_string())),
        };

        // A human-readable summary: one line per file, then the count/host/dir tail.
        use std::fmt::Write;
        let host = result.host.as_deref().unwrap_or("the page");
        let mut summary = String::new();
        for file in &result.files {
            match (file.width, file.height) {
                (Some(w), Some(h)) => {
                    let _ = writeln!(summary, "wrote {} ({w}x{h} px)", file.path);
                }
                _ => {
                    let _ = writeln!(summary, "wrote {}", file.path);
                }
            }
        }
        let _ = write!(summary, "scraped {} file(s) from {host}", result.files.len());
        if let Some(dir) = &result.dir {
            let _ = write!(summary, " into {dir}");
        }

        let payload = ScrapePayload {
            dir: result.dir.as_deref(),
            host: result.host.as_deref(),
            files: &result.files,
        };
        ok_result(summary, payload)
    }
}

#[tool_handler]
impl ServerHandler for StencilServer {
    fn get_info(&self) -> ServerInfo {
        // ServerInfo is #[non_exhaustive]; start from the default and set our fields.
        let mut info = ServerInfo::default();
        info.protocol_version = ProtocolVersion::LATEST;
        info.capabilities = ServerCapabilities::builder().enable_tools().build();
        // Identify as this crate (from_build_env() would report rmcp's own name/version).
        let mut implementation = Implementation::default();
        implementation.name = env!("CARGO_PKG_NAME").to_string();
        implementation.version = env!("CARGO_PKG_VERSION").to_string();
        info.server_info = implementation;
        info.instructions = Some(
            PROSE["instructions"]
                .as_str()
                .expect("toolDescriptions.json: \"instructions\" must be a string")
                .to_string(),
        );
        info
    }
}

/// Shared test helpers: read a `CallToolResult` back as the exact JSON an MCP client
/// receives (`CallToolResult` serializes to the real wire shape).
#[cfg(test)]
pub(super) mod testwire {
    use rmcp::model::CallToolResult;
    use serde_json::Value;

    /// The JSON an MCP client would see for a tool result.
    pub fn wire(result: &CallToolResult) -> Value {
        serde_json::to_value(result).expect("a tool result serializes")
    }

    /// The human-readable summary: the first content block.
    pub fn summary_of(result: &CallToolResult) -> String {
        wire(result)["content"][0]["text"]
            .as_str()
            .expect("the first block is the text summary")
            .to_string()
    }

    /// The structured payload: the second block, whose text is serialized JSON.
    pub fn payload_of(result: &CallToolResult) -> Value {
        let raw = wire(result)["content"][1]["text"]
            .as_str()
            .expect("the second block is the JSON payload")
            .to_string();
        serde_json::from_str(&raw).expect("the payload block holds valid JSON")
    }
}

#[cfg(test)]
mod tests {
    //! The shared result wrappers and the edit/scrape payload shapes (pure) — the contract
    //! with a calling agent, so every assertion runs against the real wire shape.

    use super::testwire::{payload_of, wire};
    use super::*;
    use serde_json::json;

    // ── ok_result / err_result ──

    #[test]
    fn ok_result_carries_a_text_summary_then_the_json_payload() {
        let result = ok_result("wrote out.png (2x2)".into(), json!({"path":"out.png"})).unwrap();
        let wire = wire(&result);

        assert_eq!(wire["isError"], false);
        assert_eq!(wire["content"].as_array().unwrap().len(), 2);
        assert_eq!(wire["content"][0]["type"], "text");
        assert_eq!(wire["content"][0]["text"], "wrote out.png (2x2)");
        assert_eq!(payload_of(&result), json!({"path":"out.png"}));
    }

    #[test]
    fn err_result_is_flagged_and_carries_only_the_message() {
        let wire = wire(&err_result("output already exists".into()));
        assert_eq!(wire["isError"], true);
        assert_eq!(wire["content"].as_array().unwrap().len(), 1);
        assert_eq!(wire["content"][0]["text"], "output already exists");
    }

    // ── The edit / scrape payloads ──

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

    #[test]
    fn scrape_payload_shape_and_nulls() {
        let files = vec![
            ScrapedFile {
                path: "a.png".into(),
                width: Some(4),
                height: Some(8),
            },
            // Video has no measured dimensions.
            ScrapedFile {
                path: "b.mp4".into(),
                width: None,
                height: None,
            },
        ];
        let value = serde_json::to_value(ScrapePayload {
            dir: Some("/tmp/scrape"),
            host: Some("example.com"),
            files: &files,
        })
        .unwrap();

        assert_eq!(value["dir"], "/tmp/scrape");
        assert_eq!(value["host"], "example.com");
        assert_eq!(value["files"][0], json!({"path":"a.png","width":4,"height":8}));
        assert_eq!(value["files"][1], json!({"path":"b.mp4","width":null,"height":null}));

        // A scrape whose summary line was absent reports nulls, not missing keys.
        let bare = serde_json::to_value(ScrapePayload {
            dir: None,
            host: None,
            files: &[],
        })
        .unwrap();
        assert_eq!(bare, json!({"dir":null,"host":null,"files":[]}));
    }
}
