//! `stencil_script_emit`: the script rewritten as a runnable file for another surface, its
//! target picked by the output's extension (`cli/CONTRACT.md` §4.4).

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use schemars::JsonSchema;
use serde::Serialize;

use super::ScriptFile;
use crate::args::{self, ScriptEmitParams};
use crate::confine::Roots;
use crate::pipeline::{self, ProcessRunner};
use crate::server::tools::{err_result, ok_result};

/// The `stencil_script_emit` payload: the written file and its target (`python` or
/// `javascript`).
#[derive(Serialize, JsonSchema)]
pub struct EmitPayload {
    path: String,
    target: String,
}

pub async fn run(roots: &Roots, mut params: ScriptEmitParams) -> Result<CallToolResult, McpError> {
    if let Err(error) = params.source.validate() {
        return Ok(err_result(error.to_string()));
    }
    let root = match roots.place("output", &params.output) {
        Ok((root, output)) => {
            params.output = output.to_string_lossy().into_owned();
            root
        }
        Err(message) => return Ok(err_result(message)),
    };
    let file = match ScriptFile::from_source(&params.source, roots) {
        Ok(file) => file,
        Err(message) => return Ok(err_result(message)),
    };
    let argv = match args::build_emit_argv(&params, &file.path) {
        Ok(argv) => argv,
        Err(error) => return Ok(err_result(error.to_string())),
    };
    match pipeline::script::emit(&ProcessRunner, &argv, &root).await {
        Ok(emitted) => {
            let summary = format!("wrote {} ({})", emitted.path, emitted.target);
            ok_result(summary, EmitPayload { path: emitted.path, target: emitted.target })
        }
        Err(message) => Ok(err_result(message)),
    }
}
