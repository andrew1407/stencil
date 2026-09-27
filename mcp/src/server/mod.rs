//! The MCP surface: `StencilServer` and its `#[tool]` methods. Each method only names its
//! parameters, prose, annotations and output schema; the work is a free function in
//! [`tools`], and every call runs inside the wrapper in [`call`].

mod call;
mod catalog;
mod handler;
mod roots;
mod tools;

pub use tools::prompt::{execute_plan, run_prompt, PromptResult};

use rmcp::handler::server::router::tool::ToolRouter;
use rmcp::handler::server::tool::Extension;
use rmcp::handler::server::wrapper::Parameters;
use rmcp::model::CallToolResult;
use rmcp::{tool, tool_router, ErrorData as McpError};

use crate::args::{
    EditParams, ProbeParams, ProjectFileParams, ProjectUpdateParams, ProjectsParams, PromptParams,
    ScrapeParams, ScriptCheckParams, ScriptEmitParams, ScriptParams, ScriptPlanParams,
};
use crate::config::Config;
use crate::confine::Roots;
use tools::{
    edit, probe, project_file, project_update, projects, prompt, schema, script, source_site,
};

/// This surface's user-facing prose, embedded from the committed canonical asset. rmcp's
/// `#[tool]` takes a literal, so descriptions ride in via `#[doc = include_str!]` shards.
static PROSE: std::sync::LazyLock<serde_json::Value> = std::sync::LazyLock::new(|| {
    serde_json::from_str(include_str!("../../toolDescriptions.json"))
        .expect("mcp/toolDescriptions.json is not valid JSON")
});

/// The Stencil MCP server. Cloneable so the transport can share it across requests; its only
/// state is the resolved configuration and the generated tool router.
#[derive(Clone)]
pub struct StencilServer {
    config: Config,
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
        Self { config, tool_router: Self::tool_router() }
    }

    #[doc = include_str!("../../toolDescriptions/stencil_edit.txt")]
    #[tool(
        annotations(read_only_hint = false, destructive_hint = true, open_world_hint = true),
        output_schema = schema::<edit::EditPayload<'static>>()
    )]
    async fn stencil_edit(
        &self,
        Extension(roots): Extension<Roots>,
        Parameters(params): Parameters<EditParams>,
    ) -> Result<CallToolResult, McpError> {
        edit::run(&self.config, &roots, params).await
    }

    #[doc = include_str!("../../toolDescriptions/stencil_probe.txt")]
    #[tool(
        annotations(read_only_hint = true, open_world_hint = true),
        output_schema = schema::<probe::ProbePayload>()
    )]
    async fn stencil_probe(
        &self,
        Extension(roots): Extension<Roots>,
        Parameters(params): Parameters<ProbeParams>,
    ) -> Result<CallToolResult, McpError> {
        probe::run(&roots, params).await
    }

    #[doc = include_str!("../../toolDescriptions/stencil_prompt.txt")]
    #[tool(
        annotations(read_only_hint = false, destructive_hint = true, open_world_hint = true),
        output_schema = schema::<prompt::PromptPayload<'static>>()
    )]
    async fn stencil_prompt(
        &self,
        Extension(roots): Extension<Roots>,
        Parameters(params): Parameters<PromptParams>,
    ) -> Result<CallToolResult, McpError> {
        prompt::run(&self.config, &roots, params).await
    }

    #[doc = include_str!("../../toolDescriptions/stencil_script.txt")]
    #[tool(
        annotations(read_only_hint = false, destructive_hint = true, open_world_hint = true),
        output_schema = schema::<script::ScriptPayload<'static>>()
    )]
    async fn stencil_script(
        &self,
        Extension(roots): Extension<Roots>,
        Parameters(params): Parameters<ScriptParams>,
    ) -> Result<CallToolResult, McpError> {
        script::run(&roots, params).await
    }

    #[doc = include_str!("../../toolDescriptions/stencil_script_check.txt")]
    #[tool(
        annotations(read_only_hint = true, open_world_hint = false),
        output_schema = schema::<script::check::CheckPayload<'static>>()
    )]
    async fn stencil_script_check(
        &self,
        Extension(roots): Extension<Roots>,
        Parameters(params): Parameters<ScriptCheckParams>,
    ) -> Result<CallToolResult, McpError> {
        script::check::run(&roots, params).await
    }

    #[doc = include_str!("../../toolDescriptions/stencil_script_plan.txt")]
    #[tool(
        annotations(read_only_hint = true, open_world_hint = true),
        output_schema = schema::<script::plan::PlanPayload>()
    )]
    async fn stencil_script_plan(
        &self,
        Extension(roots): Extension<Roots>,
        Parameters(params): Parameters<ScriptPlanParams>,
    ) -> Result<CallToolResult, McpError> {
        script::plan::run(&roots, params).await
    }

    #[doc = include_str!("../../toolDescriptions/stencil_script_emit.txt")]
    #[tool(
        annotations(read_only_hint = false, destructive_hint = true, open_world_hint = false),
        output_schema = schema::<script::emit::EmitPayload>()
    )]
    async fn stencil_script_emit(
        &self,
        Extension(roots): Extension<Roots>,
        Parameters(params): Parameters<ScriptEmitParams>,
    ) -> Result<CallToolResult, McpError> {
        script::emit::run(&roots, params).await
    }

    #[doc = include_str!("../../toolDescriptions/source_site.txt")]
    #[tool(
        annotations(read_only_hint = false, destructive_hint = false, open_world_hint = true),
        output_schema = schema::<source_site::ScrapePayload<'static>>()
    )]
    async fn source_site(
        &self,
        Extension(roots): Extension<Roots>,
        Parameters(params): Parameters<ScrapeParams>,
    ) -> Result<CallToolResult, McpError> {
        source_site::run(&roots, params).await
    }

    #[doc = include_str!("../../toolDescriptions/stencil_projects.txt")]
    #[tool(
        annotations(read_only_hint = true, open_world_hint = true),
        output_schema = schema::<projects::ProjectsPayload>()
    )]
    async fn stencil_projects(
        &self,
        Parameters(params): Parameters<ProjectsParams>,
    ) -> Result<CallToolResult, McpError> {
        projects::run(&self.config.servers, params).await
    }

    #[doc = include_str!("../../toolDescriptions/stencil_project_update.txt")]
    #[tool(
        annotations(read_only_hint = false, destructive_hint = true, open_world_hint = true),
        output_schema = schema::<project_update::UpdatePayload>()
    )]
    async fn stencil_project_update(
        &self,
        Parameters(params): Parameters<ProjectUpdateParams>,
    ) -> Result<CallToolResult, McpError> {
        project_update::run(&self.config.servers, params).await
    }

    #[doc = include_str!("../../toolDescriptions/stencil_project_file.txt")]
    #[tool(
        annotations(read_only_hint = false, destructive_hint = true, open_world_hint = true),
        output_schema = schema::<project_file::FilePayload>()
    )]
    async fn stencil_project_file(
        &self,
        Extension(roots): Extension<Roots>,
        Parameters(params): Parameters<ProjectFileParams>,
    ) -> Result<CallToolResult, McpError> {
        project_file::run(&self.config.servers, &roots, params).await
    }
}

#[cfg(test)]
pub(super) mod testwire;
