//! The MCP surface: a server exposing `stencil_edit`, `stencil_probe`, `stencil_prompt`
//! and `source_site` over stdio. Each `#[tool]` method here only names its parameters and
//! its prose; the work is a free function in [`tools`].

mod tools;

pub use tools::prompt::run_prompt;

use rmcp::handler::server::router::tool::ToolRouter;
use rmcp::handler::server::wrapper::Parameters;
use rmcp::model::{
    CallToolResult, Implementation, ProtocolVersion, ServerCapabilities, ServerInfo,
};
use rmcp::{tool, tool_handler, tool_router, ErrorData as McpError, ServerHandler};

use crate::args::{EditParams, ProbeParams, PromptParams, ScrapeParams};
use crate::config::Config;

/// This surface's user-facing prose, embedded from the committed canonical asset. rmcp's
/// `#[tool]` takes a literal, so the four descriptions ride in on `#[doc = include_str!]`
/// from its generated `toolDescriptions/*.txt` shards; `tests/tool_prose_test.rs` pins
/// those and README.md's Tools table against this file.
static PROSE: std::sync::LazyLock<serde_json::Value> = std::sync::LazyLock::new(|| {
    serde_json::from_str(include_str!("../../toolDescriptions.json"))
        .expect("mcp/toolDescriptions.json is not valid JSON")
});

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
        tools::edit::run(&self.config, params).await
    }

    #[doc = include_str!("../../toolDescriptions/stencil_probe.txt")]
    #[tool]
    async fn stencil_probe(
        &self,
        Parameters(params): Parameters<ProbeParams>,
    ) -> Result<CallToolResult, McpError> {
        tools::probe::run(params).await
    }

    #[doc = include_str!("../../toolDescriptions/stencil_prompt.txt")]
    #[tool]
    async fn stencil_prompt(
        &self,
        Parameters(params): Parameters<PromptParams>,
    ) -> Result<CallToolResult, McpError> {
        tools::prompt::run(&self.config, params).await
    }

    #[doc = include_str!("../../toolDescriptions/source_site.txt")]
    #[tool]
    async fn source_site(
        &self,
        Parameters(params): Parameters<ScrapeParams>,
    ) -> Result<CallToolResult, McpError> {
        tools::source_site::run(params).await
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
