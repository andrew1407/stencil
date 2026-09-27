//! `stencil_script_plan`: the script lowered to op-plans (`cli/CONTRACT.md` §4.3) — what a
//! run would do, with nothing fetched and nothing written.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use serde_json::Value;

use super::ScriptFile;
use crate::args::{self, ScriptPlanParams};
use crate::confine::Roots;
use crate::pipeline::{self, ProcessRunner};
use crate::server::tools::{err_result, ok_result};

/// The CLI's plan envelope: its version, the script, every diagnostic, and one entry per
/// `@source` block with its inputs, op-plans and concrete saves.
#[derive(Serialize, Deserialize, JsonSchema)]
pub struct PlanPayload {
    #[serde(default)]
    version: u64,
    #[serde(default)]
    script: String,
    #[serde(default)]
    diagnostics: Vec<Value>,
    #[serde(default)]
    blocks: Vec<Value>,
}

pub async fn run(roots: &Roots, mut params: ScriptPlanParams) -> Result<CallToolResult, McpError> {
    if let Err(error) = params.source.validate() {
        return Ok(err_result(error.to_string()));
    }
    params.input = params.input.as_deref().map(|input| roots.resolve(input));
    let file = match ScriptFile::from_source(&params.source, roots) {
        Ok(file) => file,
        Err(message) => return Ok(err_result(message)),
    };
    let argv = match args::build_plan_argv(&params, &file.path) {
        Ok(argv) => argv,
        Err(error) => return Ok(err_result(error.to_string())),
    };
    let envelope = match pipeline::script::plan(&ProcessRunner, &argv, roots.primary()).await {
        Ok(envelope) => envelope,
        Err(message) => return Ok(err_result(message)),
    };
    let mut plan: PlanPayload = match serde_json::from_value(envelope) {
        Ok(plan) => plan,
        Err(e) => return Ok(err_result(format!("the CLI's plan envelope did not parse: {e}"))),
    };
    // Inline text ran from a temp file whose name means nothing to the caller.
    if file.inline {
        plan.script = "<inline>".to_string();
    }
    ok_result(summary(&plan), plan)
}

/// One line for the plan as a whole, then one per block.
fn summary(plan: &PlanPayload) -> String {
    let count = |block: &Value, key: &str| block[key].as_array().map_or(0, Vec::len);
    let errors = plan.diagnostics.iter().filter(|d| d["severity"] == "error").count();
    let mut text = format!(
        "{} diagnostic(s), {errors} error(s); {} block(s)",
        plan.diagnostics.len(),
        plan.blocks.len()
    );
    for block in &plan.blocks {
        let actions: usize = block["plans"]
            .as_array()
            .map_or(0, |plans| plans.iter().map(|p| count(p, "actions")).sum());
        text.push_str(&format!(
            "\nblock {} ({}): {} input(s), {actions} action(s), {} save(s)",
            block["index"],
            block["source"].as_str().filter(|s| !s.is_empty()).unwrap_or("the input"),
            count(block, "inputs"),
            count(block, "saves"),
        ));
    }
    text
}
