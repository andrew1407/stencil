//! The `ServerHandler`: identity and capabilities, every tool call through the wrapper in
//! [`super::call`], and the resources and prompts of [`super::catalog`].

use rmcp::handler::server::tool::ToolCallContext;
use rmcp::model::{
    CallToolRequestParams, CallToolResult, GetPromptRequestParams, GetPromptResult,
    Implementation, ListPromptsResult, ListResourcesResult, PaginatedRequestParams,
    ProtocolVersion, ReadResourceRequestParams, ReadResourceResult, ServerCapabilities,
    ServerInfo,
};
use rmcp::service::RequestContext;
use rmcp::{tool_handler, ErrorData as McpError, RoleServer, ServerHandler};

use super::{call, catalog, StencilServer, PROSE};

#[tool_handler(router = self.tool_router)]
impl ServerHandler for StencilServer {
    fn get_info(&self) -> ServerInfo {
        // ServerInfo is #[non_exhaustive]; start from the default and set our fields.
        let mut info = ServerInfo::default();
        info.protocol_version = ProtocolVersion::LATEST;
        let capabilities = ServerCapabilities::builder().enable_tools().enable_resources();
        info.capabilities = capabilities.enable_prompts().build();
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

    async fn call_tool(
        &self,
        request: CallToolRequestParams,
        context: RequestContext<RoleServer>,
    ) -> Result<CallToolResult, McpError> {
        call::decorate(context, &self.config.roots, |context| {
            self.tool_router.call(ToolCallContext::new(self, request, context))
        })
        .await
    }

    async fn list_resources(
        &self,
        _request: Option<PaginatedRequestParams>,
        _context: RequestContext<RoleServer>,
    ) -> Result<ListResourcesResult, McpError> {
        Ok(ListResourcesResult::with_all_items(catalog::resources()))
    }

    async fn read_resource(
        &self,
        request: ReadResourceRequestParams,
        _context: RequestContext<RoleServer>,
    ) -> Result<ReadResourceResult, McpError> {
        catalog::read(&request.uri)
    }

    async fn list_prompts(
        &self,
        _request: Option<PaginatedRequestParams>,
        _context: RequestContext<RoleServer>,
    ) -> Result<ListPromptsResult, McpError> {
        Ok(ListPromptsResult::with_all_items(catalog::prompts()))
    }

    async fn get_prompt(
        &self,
        request: GetPromptRequestParams,
        _context: RequestContext<RoleServer>,
    ) -> Result<GetPromptResult, McpError> {
        catalog::prompt(&request.name, request.arguments.as_ref())
    }
}
