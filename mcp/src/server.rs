//! The MCP surface: a server exposing `stencil_edit` and `stencil_probe` over stdio.

use rmcp::handler::server::router::tool::ToolRouter;
use rmcp::handler::server::wrapper::Parameters;
use rmcp::model::{
    CallToolResult, Content, Implementation, ProtocolVersion, ServerCapabilities, ServerInfo,
};
use rmcp::{tool, tool_handler, tool_router, ErrorData as McpError, ServerHandler};
use serde::Serialize;

use crate::args::{EditParams, ProbeParams, ScrapeParams};
use crate::config::Config;
use crate::deliver::DeliveryNote;
use crate::outcome::{Remote, ScrapedFile};
use crate::{deliver, pipeline};

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
        → crop → rotate → draw layout → filter → encode) and write the result to a file, \
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
             (source → crop → rotate → layout → filter → encode), writes a file, and \
             delivers it to the configured surface(s) — cli (file), desktop (launch the Qt \
             app), browser (editor launch URL); pass `surface` to override per call. It can \
             also work with Stencil collaboration servers: `server`+`input` fetches a \
             project by name to edit, `remote_update` writes the result back, and `remote` \
             (+`remote_name`) publishes the result as a new project — `server` and `remote` \
             can be different servers in one call. `stencil_probe` returns an image's pixel \
             size. `source_site` scrapes a web page and downloads its matching media (filter \
             by category/format/dimensions, page with count/group) into a directory. \
             Coordinates in layouts and crops are image pixels. The server shells out \
             to the Stencil CLI, so set STENCIL_CLI if the binary isn't found in the repo or \
             on PATH."
                .to_string(),
        );
        info
    }
}
