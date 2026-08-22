//! Op-plan parser/validator and its mapping onto the CLI pipeline.
//!
//! Port of `llm-contract.md` §1–§3 — behaviorally mirrors the browser reference
//! (`browser/js/llm/opPlan.js`): Markdown fences are stripped, the first balanced `{…}`
//! JSON object wins, text with no JSON object (or unparseable braces) is a *chat-only*
//! turn, unknown ops are skipped with a warning, and a known op with invalid params fails
//! the whole plan — except §1's one leniency: a variant (or an ask-option preview) holding
//! a top-level-only op is dropped with a warning and the rest of the plan still runs.
//! Limits are the contract's: ≤ 16 actions, ≤ 8 variants, ≤ 200 layout
//! lines, ≤ 32 frame indices, ≤ 5000 chars per string field.
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
//! Split by stage: [`parse`] (extraction + plan shape), [`actions`] (per-op validators),
//! [`ask`] (§11 cards), [`lower`] (the mapping onto CLI runs), [`types`] (the validated
//! plan + errors). The limits below are shared by all of them.

mod actions;
mod ask;
mod lower;
mod parse;
mod types;

pub use ask::format_ask;
pub use lower::{sanitize_label, to_edit_requests, EditRequest};
pub use parse::parse_op_plan;
pub use types::{
    Action, AskCard, AskOption, Axis, Dir, FilterMode, FormulaOp, OpPlan, OpPlanError, PageSize,
    Variant,
};

// Limits — the same numbers in every client (contract §1).
pub const MAX_ACTIONS: usize = 16;
pub const MAX_VARIANTS: usize = 8;
pub const MAX_LAYOUT_LINES: usize = 200;
pub const MAX_FRAME_INDICES: usize = 32;
pub const MAX_STRING_CHARS: usize = 5000;
/// §2.1 `save` name cap — the same 120 characters in every client.
pub const MAX_SAVE_NAME: usize = 120;
/// §10: the longest local path a `save` op may carry — the same 1024 in every client.
pub const MAX_PATH_CHARS: usize = 1024;
// §2 custom page/blank dims — inclusive cm bounds, the same numbers in every client.
pub const DIM_CM_MIN: f64 = 0.1;
pub const DIM_CM_MAX: f64 = 500.0;
// §11 interactive replies — the same numbers as every other client.
pub const MIN_ASK_OPTIONS: usize = 2;
pub const MAX_ASK_OPTIONS: usize = 5;
pub const MAX_ASK_QUESTION: usize = 300;
pub const MAX_ASK_LABEL: usize = 80;
pub const DEFAULT_CUSTOM_LABEL: &str = "Something else…";

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
