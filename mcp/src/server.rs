//! The MCP surface: a server exposing `stencil_edit` and `stencil_probe` over stdio.

use rmcp::handler::server::router::tool::ToolRouter;
use rmcp::handler::server::wrapper::Parameters;
use rmcp::model::{
    CallToolResult, Content, Implementation, ProtocolVersion, ServerCapabilities, ServerInfo,
};
use rmcp::{tool, tool_handler, tool_router, ErrorData as McpError, ServerHandler};

use crate::args::{EditParams, ProbeParams};
use crate::config::Config;
use crate::{deliver, pipeline};

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
        `output` path, and an optional `surface` override. Returns the written path, final \
        dimensions, and per-surface delivery notes."
    )]
    async fn stencil_edit(
        &self,
        Parameters(params): Parameters<EditParams>,
    ) -> Result<CallToolResult, McpError> {
        let result = match pipeline::run_edit(&params).await {
            Ok(result) => result,
            Err(message) => return Ok(CallToolResult::error(vec![Content::text(message)])),
        };

        let surfaces = match params.resolve_surfaces(&self.config.default_surfaces) {
            Ok(surfaces) => surfaces,
            Err(message) => return Ok(CallToolResult::error(vec![Content::text(message)])),
        };

        let notes = deliver::deliver(&surfaces, &result, &self.config).await;

        // A human-readable summary: the write line, then one line per surface beyond cli.
        use std::fmt::Write;
        let mut summary = format!("wrote {} ({}x{})", result.path, result.width, result.height);
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

        let deliveries: Vec<_> = notes
            .iter()
            .map(|n| {
                serde_json::json!({
                    "surface": n.surface,
                    "ok": n.ok,
                    "detail": n.detail,
                    "url": n.url,
                })
            })
            .collect();
        let payload = serde_json::json!({
            "path": result.path,
            "width": result.width,
            "height": result.height,
            "surfaces": surfaces.iter().map(|s| s.as_str()).collect::<Vec<_>>(),
            "deliveries": deliveries,
        });

        Ok(CallToolResult::success(vec![
            Content::text(summary),
            Content::json(payload)?,
        ]))
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
                Ok(CallToolResult::success(vec![
                    Content::text(summary),
                    Content::json(payload)?,
                ]))
            }
            Err(message) => Ok(CallToolResult::error(vec![Content::text(message)])),
        }
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
             app), browser (editor launch URL); pass `surface` to override per call. \
             `stencil_probe` returns an image's pixel size. Coordinates in layouts and \
             crops are image pixels. The server shells out to the Stencil CLI, so set \
             STENCIL_CLI if the binary isn't found in the repo or on PATH."
                .to_string(),
        );
        info
    }
}
