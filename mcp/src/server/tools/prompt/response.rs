//! What a `stencil_prompt` turn reports back: the payload types and the single
//! success exit. The §7 round merges are in `merge.rs`.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use schemars::JsonSchema;
use serde::Serialize;

use crate::opplan;
use crate::server::tools::ok_result;

/// One written result of a `stencil_prompt` plan: the base result (`label` null), a variant
/// (its sanitized label), or a §2.1 `save`'s `.stencil` — a document, so no dimensions.
#[derive(Serialize, JsonSchema)]
pub struct PromptResult {
    pub label: Option<String>,
    pub path: String,
    pub width: Option<u32>,
    pub height: Option<u32>,
}

/// The `stencil_prompt` structured payload: the chat reply, any non-fatal notes, and the
/// written results (empty for a chat-only turn).
#[derive(Serialize, JsonSchema)]
pub struct PromptPayload<'a> {
    reply: &'a str,
    notes: &'a [String],
    results: &'a [PromptResult],
    /// The turn's question (contract §11), when it asked one. No interactive surface here:
    /// the agent gets the card as data and answers by calling `stencil_prompt` again.
    #[serde(skip_serializing_if = "Option::is_none")]
    ask: Option<PromptAsk<'a>>,
}

/// The `ask` card as the tool reports it: the question, whether several picks are allowed,
/// and the option LABELS (previews are not shown by this server — contract §11.4).
#[derive(Serialize, JsonSchema)]
struct PromptAsk<'a> {
    question: &'a str,
    multi: bool,
    allow_custom: bool,
    options: Vec<&'a str>,
}

/// Borrow a validated card into the payload shape.
fn prompt_ask(card: &opplan::AskCard) -> PromptAsk<'_> {
    PromptAsk {
        question: &card.question,
        multi: card.multi,
        allow_custom: card.allow_custom,
        options: card.options.iter().map(|o| o.label.as_str()).collect(),
    }
}

/// The single success exit of `stencil_prompt`: the reply, any notes, one `wrote` line per
/// result, and — since a plan may both edit and ask (§11.3) — the ask card last.
pub(super) fn prompt_response(
    plan: &opplan::OpPlan,
    notes: &[String],
    results: &[PromptResult],
) -> Result<CallToolResult, McpError> {
    use std::fmt::Write;
    let mut summary = plan.reply.clone();
    for note in notes {
        let _ = write!(summary, "\n{note}");
    }
    for result in results {
        match (result.width, result.height) {
            (Some(width), Some(height)) => {
                let _ = write!(summary, "\nwrote {} ({width}x{height})", result.path);
            }
            // A §2.1 `save`: the CLI's own project line, verbatim.
            _ => {
                let _ = write!(summary, "\nwrote {} (project)", result.path);
            }
        }
    }
    if let Some(card) = &plan.ask {
        summary.push_str(&opplan::format_ask(card));
    }
    let payload = PromptPayload {
        reply: &plan.reply,
        notes,
        results,
        ask: plan.ask.as_ref().map(prompt_ask),
    };
    ok_result(summary, payload)
}

#[cfg(test)]
#[path = "response_tests.rs"]
mod tests;
