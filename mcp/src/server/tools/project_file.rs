//! `stencil_project_file`: download one file a project stores on an allowlisted collaboration
//! server into the call's roots, through the CLI's `--project-file` spawned confined like an
//! edit, `--no-clobber` unless the caller asks to overwrite.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};

use super::projects::entry;
use super::{err_result, ok_result};
use crate::args::{self, ProjectFileParams};
use crate::config::Servers;
use crate::confine::Roots;
use crate::pipeline;

/// The `stencil_project_file` payload: what was fetched and where it now lies.
#[derive(Serialize, Deserialize, JsonSchema)]
pub struct FilePayload {
    #[serde(default)]
    server: String,
    id: String,
    kind: String,
    /// The absolute path written, inside the roots.
    path: String,
    bytes: u64,
    /// The format the file's first bytes name (`png`, `jpg`, …), when they name one.
    format: Option<String>,
}

pub async fn run(
    servers: &Servers,
    roots: &Roots,
    params: ProjectFileParams,
) -> Result<CallToolResult, McpError> {
    let entry = match entry(servers, params.server.as_deref()) {
        Ok(entry) => entry,
        Err(message) => return Ok(err_result(message)),
    };
    if params.output.starts_with('-') {
        let dashed = args::EditError::DashValue("output", params.output.clone());
        return Ok(err_result(dashed.to_string()));
    }
    let (root, output) = match roots.place("output", &params.output) {
        Ok(placed) => placed,
        Err(message) => return Ok(err_result(message)),
    };
    let output = output.to_string_lossy().into_owned();
    let argv = match args::build_project_file_argv(&entry.origin, &params, &output) {
        Ok(argv) => argv,
        Err(error) => return Ok(err_result(error.to_string())),
    };
    let tokens = Servers::child_tokens([entry]);
    let root = root.to_string_lossy();
    let document = match pipeline::run_project_file(&argv, &root, tokens.as_deref()).await {
        Ok(document) => document,
        Err(message) => return Ok(err_result(message)),
    };
    let mut payload: FilePayload = match serde_json::from_value(document) {
        Ok(payload) => payload,
        Err(e) => {
            return Ok(err_result(format!("the stencil CLI's download report is unreadable: {e}")));
        }
    };
    payload.server = entry.origin.clone();
    let format = payload.format.as_deref().unwrap_or("unknown format");
    let summary = format!(
        "wrote {} ({} bytes, {format}) — the {} file of {} on {}",
        payload.path, payload.bytes, payload.kind, payload.id, payload.server
    );
    ok_result(summary, payload)
}
