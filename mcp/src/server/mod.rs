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
/// result/notes so serialization is the single source of the payload shape (the per-delivery
/// and per-server objects come straight off `DeliveryNote`/`Remote`'s own `Serialize`).
#[derive(Serialize)]
struct EditPayload<'a> {
    path: &'a str,
    width: u32,
    height: u32,
    surfaces: Vec<&'static str>,
    deliveries: &'a [DeliveryNote],
    server: &'a [Remote],
}

/// The `source_site` structured payload: the destination directory, the scraped page's
/// host, and every downloaded file (each with its measured dimensions, or null for video).
/// `files` borrows the pipeline result's `ScrapedFile`s, whose own `Serialize` shapes each
/// `{path,width,height}` object.
#[derive(Serialize)]
struct ScrapePayload<'a> {
    dir: Option<&'a str>,
    host: Option<&'a str>,
    files: &'a [ScrapedFile],
}

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

    #[tool(
        description = "Edit one image or video frame with Stencil's core pipeline (source \
        → crop → rotate → filter → draw layout → encode) and write the result to a file, \
        then deliver it to the selected surface(s). Provide either `input` (a path or \
        http(s) URL) or `blank` (a fresh canvas), plus any of crop/rotate/layout/filter, an \
        `output` path, and an optional `surface` override. To work with a Stencil \
        collaboration server: set `server` to a server URL and `input` to a project NAME to \
        fetch and edit it, add `remote_update` to write the result back; or set `remote` (a \
        server URL) + optional `remote_name` to push the result as a NEW project. `server` \
        and `remote` may point at different servers, so one call can fetch from one and \
        publish to another. The result is always saved locally too. Returns the written \
        path, final dimensions, per-surface delivery notes, and any server projects \
        updated/created."
    )]
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

        // A human-readable summary: the write line + any server deliveries (from the result),
        // then one line per surface beyond cli.
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

        // The structured payload mirrors the shapes of `DeliveryNote` and `Remote` directly;
        // `server` may touch more than one collaboration server in one call (fetch/update one
        // and create on another), each object tagged with its action by `Remote`'s Serialize.
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

    #[tool(
        description = "Read an image's pixel dimensions. Useful before computing crop or \
        layout coordinates. `input` is a path or http(s) URL. Returns width and height."
    )]
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

    #[tool(
        description = "Ask Stencil's configured LLM to plan and run edits from a natural-\
        language `prompt` (see llm-contract.md). `input` is the working image (path \
        or http(s) URL; a local file is also attached for vision). The LLM answers with a \
        strictly validated op-plan (crop / rotate / filter / layout / blank / frame / \
        image / save); its base actions are written to `{output_dir}/result.png` and each \
        variant to `{output_dir}/{label}.png` via the same CLI pipeline as stencil_edit, \
        and each `save` op bundles the image + layout so far into \
        `{output_dir}/{name}.stencil`. This tool carries ONE `input`, so an `image` op may \
        only select index 1 (it restarts from that input); a higher index is noted and \
        skipped. A plan with \
        no actions is a chat-only answer (text, nothing written). A plan that only LOADS \
        a picture (blank/frame, no layout drawn) is applied and the prompt automatically \
        re-sent ONCE with the loaded image attached (llm-contract §7), so e.g. 'create a \
        blank page and draw …' completes in one call. The provider and its \
        endpoint come from the operator's STENCIL_LLM_* env and are NOT settable per call \
        (only `model` is): `ollama`, `openai-compat` (LM Studio etc.), or `stencil-server` \
        (a Stencil collaboration server proxying Anthropic). The built-in transport is \
        plain http:// only and refuses to send credentials off-loopback."
    )]
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

    #[tool(
        description = "Scrape a web page and download the media it references into a \
        DIRECTORY. Give `source_site` (the page's http(s) URL) and an `output` directory \
        (created if missing; defaults to the current directory). Filter what's downloaded \
        with `filter` (category tokens `img|video|background|poster`, `|`-separated; default \
        all), `format` (normalized extension tokens like `png|jpg|webp|mp4`; default all), \
        and `min_width`/`max_width`/`min_height`/`max_height` (inclusive px bounds measured \
        from image bytes; video and unmeasurable items always pass). Page through large \
        result sets with `count` (items per page — omit to take ALL matches) and `group` (a \
        0-based page index). This is a headless directory download, so it only writes files \
        locally (the `cli` surface). Returns the directory, the page host, and the list of \
        written files with their pixel dimensions."
    )]
    async fn source_site(
        &self,
        Parameters(params): Parameters<ScrapeParams>,
    ) -> Result<CallToolResult, McpError> {
        // Scraping writes a directory of downloads — the only supported delivery is the local
        // file write. Reject any other surface override before touching the CLI.
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
            "Stencil image/video editing. `stencil_edit` runs the full pipeline \
             (source → crop → rotate → filter → layout → encode), writes a file, and \
             delivers it to the configured surface(s) — cli (file), desktop (launch the Qt \
             app), browser (editor launch URL); pass `surface` to override per call. It can \
             also work with Stencil collaboration servers: `server`+`input` fetches a \
             project by name to edit, `remote_update` writes the result back, and `remote` \
             (+`remote_name`) publishes the result as a new project — `server` and `remote` \
             can be different servers in one call. `stencil_probe` returns an image's pixel \
             size. `source_site` scrapes a web page and downloads its matching media (filter \
             by category/format/dimensions, page with count/group) into a directory. \
             `stencil_prompt` hands a natural-language request (plus the input image, for \
             vision) to a configured LLM — ollama, openai-compat, or a stencil-server \
             Anthropic proxy, via the STENCIL_LLM_* env keys, plain http:// only — and runs \
             the strictly validated op-plan it returns through the same pipeline, writing \
             {output_dir}/result.png plus one file per variant. \
             Coordinates in layouts and crops are image pixels. The server shells out \
             to the Stencil CLI, so set STENCIL_CLI if the binary isn't found in the repo or \
             on PATH."
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
    //! The shared result wrappers and the edit/scrape payload shapes (pure). These types
    //! are the contract with a calling agent: it reads the text summary and parses the
    //! JSON block, so every assertion runs against the real wire shape.

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
