//! `stencil_script_check`: a script's diagnostics, nothing run and nothing written.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use schemars::JsonSchema;
use serde::Serialize;

use super::ScriptFile;
use crate::args::{self, ScriptCheckParams};
use crate::confine::Roots;
use crate::outcome::Diagnostic;
use crate::pipeline::{self, ProcessRunner};
use crate::server::tools::{err_result, ok_result};

/// The `stencil_script_check` payload: `valid` is false when any diagnostic is an error.
#[derive(Serialize, JsonSchema)]
pub struct CheckPayload<'a> {
    valid: bool,
    errors: usize,
    warnings: usize,
    diagnostics: &'a [Diagnostic],
}

pub async fn run(roots: &Roots, params: ScriptCheckParams) -> Result<CallToolResult, McpError> {
    if let Err(error) = params.source.validate() {
        return Ok(err_result(error.to_string()));
    }
    let file = match ScriptFile::from_source(&params.source, roots) {
        Ok(file) => file,
        Err(message) => return Ok(err_result(message)),
    };
    let argv = match args::build_check_argv(&params, &file.path) {
        Ok(argv) => argv,
        Err(error) => return Ok(err_result(error.to_string())),
    };
    let check = match pipeline::script::check(&ProcessRunner, &argv, roots.primary()).await {
        Ok(check) => check,
        Err(message) => return Ok(err_result(message)),
    };

    let diagnostics = &check.diagnostics;
    let count = |severity: &str| diagnostics.iter().filter(|d| d.severity == severity).count();
    let (errors, warnings) = (count("error"), count("warning"));
    let mut summary = match check.valid {
        true if warnings == 0 => "the script is valid".to_string(),
        true => format!("the script is valid, with {warnings} warning(s)"),
        false => format!("the script has {errors} error(s) and {warnings} warning(s)"),
    };
    for d in diagnostics {
        let code = if d.code.is_empty() { String::new() } else { format!(" [{}]", d.code) };
        summary.push_str(&format!("\n{}:{}: {}: {}{code}", d.line, d.col, d.severity, d.message));
    }
    let payload = CheckPayload { valid: check.valid, errors, warnings, diagnostics };
    ok_result(summary, payload)
}
