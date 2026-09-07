//! Op-plan parser/validator and its mapping onto the CLI pipeline.
//!
//! Port of `llm-contract.md` §1–§3 — behaviorally mirrors the browser reference
//! (`browser/js/llm/opPlan.js`): Markdown fences are stripped, the first balanced `{…}`
//! JSON object wins, text with no JSON object (or unparseable braces) is a *chat-only*
//! turn, unknown ops are skipped with a warning, and a known op with invalid params fails
//! the whole plan — except §1's one leniency: a variant (or an ask-option preview) holding
//! a top-level-only op is dropped with a warning and the rest of the plan still runs.
//! Limits, key schemas, enums, ranges and token grammars are TABLE-DRIVEN from the shared
//! `browser/js/config/llm/opRegistry.json` (embedded; see `schema.rs`).
//!
//! `to_edit_requests` then maps a validated plan onto [`crate::args::EditParams`] runs of
//! the existing pipeline — one CLI run for the base result and one per variant. The CLI's
//! pipeline order is fixed (source → frame → crop → rotate → filter → layout), so a plan's
//! actions are **collapsed** into that order (rotations sum, layout lines concatenate, the
//! last filter wins); a plan that genuinely needs two crops in one image, a formula SET,
//! a standalone `page`, or multiple video frames cannot be expressed as one CLI run and
//! is rejected with a clear message. The formula clear/disable forms and custom cm page/
//! blank dims (contract §2) are accepted: the dims map onto the CLI's `--blank w h`
//! pixels, and a formula clear is skipped with a note (a headless run has none to clear).
//!
//! Split by stage: [`parse`] (extraction + plan shape), [`schema`] (the registry-driven
//! check engine every op and the ask card validate through), [`actions`] (the op dispatch
//! + typed normalizers), [`ask`] (§11 cards), [`lower`] (the mapping onto CLI runs),
//! [`types`] (the validated plan + errors). Limits come from the registry.

mod actions;
mod ask;
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
/// has not seen (`blank` or `frame` here — this tool has no openUrl) and drew NO layout
/// line (an empty `layout` drew nothing — same rule as the desktop). Crop/filter/page
/// edits need no pixels, but outlining does, so the caller re-sends the turn once with
/// the rendered result attached (the cli console's `loadsWithoutTracing`). Variants are
/// finished outputs and an `ask` hands the turn to the user — neither continues.
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
