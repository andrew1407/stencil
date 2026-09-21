//! Op-plan parser/validator and its mapping onto the CLI pipeline.
//!
//! Port of `llm-contract.md` §1–§3, behaviorally mirroring `browser/js/llm/plan/plan.js`;
//! limits, key schemas, enums and token grammars are table-driven from the shared
//! `browser/js/config/llm/opRegistry.json`. `to_edit_requests` maps a plan onto CLI runs.

mod actions;
mod ask;
pub mod fold;
mod lower;
mod parse;
pub mod schema;
mod types;

pub use ask::format_ask;
pub use lower::{sanitize_label, to_edit_requests, EditRequest};
pub use parse::parse_op_plan;
pub use types::{
    Action, AskCard, AskOption, Axis, Dir, FilterMode, FormulaOp, OpPlan, OpPlanError, PageSize,
    Variant,
};

/// §11 option cap, pinned to the registry's `limits.ask.maxOptions` by `tests/schema_test.rs`
/// (every other limit is read from the registry through [`schema::Schema::limit`]).
pub const MAX_ASK_OPTIONS: usize = 5;

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
