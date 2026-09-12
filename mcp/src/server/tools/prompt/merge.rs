//! The §7 round merges: how a round-2 outcome folds onto what round 1 already wrote.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;

use crate::opplan;
use crate::server::tools::err_result;

use super::response::{prompt_response, PromptResult};

/// A round-2 failure never costs round 1's work: fold the failure into a note and answer
/// with what the first round already wrote. With no first round it stays a hard error.
pub(super) fn kept_or_error(
    first: Option<(String, Vec<PromptResult>)>,
    mut notes: Vec<String>,
    detail: String,
) -> Result<CallToolResult, McpError> {
    let Some((reply, results)) = first else {
        return Ok(err_result(detail));
    };
    notes.push(format!(
        "note: auto-continuation failed ({detail}) — kept the loaded image"
    ));
    let plan = opplan::OpPlan {
        reply,
        actions: Vec::new(),
        variants: Vec::new(),
        warnings: Vec::new(),
        chat_only: false,
        ask: None,
    };
    prompt_response(&plan, &notes, &results)
}

/// Fold a §7 round-1 outcome (when there was one) into the final response: the replies
/// join in order, and round 1's written files survive unless round 2 rewrote the path.
pub(super) fn merged_response(
    first: Option<(String, Vec<PromptResult>)>,
    plan: &opplan::OpPlan,
    notes: &[String],
    results: Vec<PromptResult>,
) -> Result<CallToolResult, McpError> {
    let Some((first_reply, first_results)) = first else {
        return prompt_response(plan, notes, &results);
    };
    let mut merged: Vec<PromptResult> = first_results
        .into_iter()
        .filter(|r| !results.iter().any(|n| n.path == r.path))
        .collect();
    merged.extend(results);
    let reply = [first_reply.as_str(), plan.reply.as_str()]
        .iter()
        .filter(|part| !part.trim().is_empty())
        .copied()
        .collect::<Vec<_>>()
        .join("\n");
    let plan = opplan::OpPlan {
        reply,
        ..plan.clone()
    };
    prompt_response(&plan, notes, &merged)
}
