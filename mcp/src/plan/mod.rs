//! The op plan (`llm-contract.md` §1–§3, §11): a model's reply judged by core through the
//! CLI's `--plan-check`, typed from its normalized result, and mapped onto CLI runs.
//!
//! Validation and its wording are core's (`core/opplan`, twin of `browser/js/llm/plan/parser.js`);
//! this module keeps the typed normalizers, the lowering and the §7 continuation rule.

mod actions;
mod ask;
mod check;
pub mod fold;
mod lower;
mod result;
mod types;

pub use ask::format_ask;
pub use check::{parse_op_plan, parse_op_plan_with, registry_skew, PLAN_CHECK_ARGV};
pub use lower::{sanitize_label, to_edit_requests, EditRequest};
pub use result::from_result;
pub use types::{
    Action, AskCard, AskOption, Axis, Dir, FilterMode, FormulaOp, OpPlan, OpPlanError, PageSize,
    Variant,
};

/// How long a sanitized variant label may get (mirrors the browser's 40-char cap).
const MAX_LABEL_CHARS: usize = 40;

// ── §7 auto-continuation ──

/// §7 auto-continuation: true when the plan's top-level actions LOAD a picture the model
/// has not seen and drew no layout line. Variants and an `ask` never continue.
pub fn loads_without_tracing(plan: &OpPlan) -> bool {
    if !plan.variants.is_empty() || plan.ask.is_some() {
        return false;
    }
    let mut loads = false;
    for action in &plan.actions {
        match action {
            Action::Blank { .. } | Action::Frame { .. } => loads = true,
            Action::Layout { lines } if !lines.is_empty() => return false,
            _ => {}
        }
    }
    loads
}
