//! `stencil_project_update`: change one project's metadata on an allowlisted collaboration
//! server through the CLI's `--project-update`, the operator's token in the child's environment,
//! and hand back the project as the server now holds it.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use schemars::JsonSchema;
use serde::Serialize;

use super::projects::{entry, ProjectMeta};
use super::{err_result, ok_result};
use crate::args::{self, ProjectUpdateParams};
use crate::config::Servers;
use crate::pipeline;

/// The `stencil_project_update` payload: the server asked and the project after the write.
#[derive(Serialize, JsonSchema)]
pub struct UpdatePayload {
    server: String,
    project: ProjectMeta,
}

pub async fn run(
    servers: &Servers,
    params: ProjectUpdateParams,
) -> Result<CallToolResult, McpError> {
    let entry = match entry(servers, params.server.as_deref()) {
        Ok(entry) => entry,
        Err(message) => return Ok(err_result(message)),
    };
    let argv = match args::build_project_update_argv(&entry.origin, &params) {
        Ok(argv) => argv,
        Err(error) => return Ok(err_result(error.to_string())),
    };
    let tokens = Servers::child_tokens([entry]);
    let document = match pipeline::run_projects(&argv, tokens.as_deref()).await {
        Ok(document) => document,
        Err(message) => return Ok(err_result(message)),
    };
    let project: ProjectMeta = match serde_json::from_value(document) {
        Ok(project) => project,
        Err(e) => {
            let origin = &entry.origin;
            return Ok(err_result(format!("{origin} sent a project this tool cannot read: {e}")));
        }
    };
    let (line, version) = (project.line(), project.version());
    let summary = format!("updated {line} on {} — now version {version}", entry.origin);
    ok_result(summary, UpdatePayload { server: entry.origin.clone(), project })
}
