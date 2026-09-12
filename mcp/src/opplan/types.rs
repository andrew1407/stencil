//! The validated plan — the types `parse_op_plan` produces — and the one error enum
//! every stage of this module fails with.

use crate::layout::Line;

// ── Errors ──

/// Everything plan parsing/mapping can fail with. Hand-written; `Display` is the exact
/// user-facing message (matching the browser's error strings where they exist).
#[derive(Debug)]
pub enum OpPlanError {
    /// A structural problem with the plan (`actions`/`variants` shape, limits).
    Plan(String),
    /// A known op with invalid params — fails the whole plan (contract §1).
    Action { op: String, detail: String },
    /// The plan is valid but cannot be expressed as CLI runs (mapping stage).
    Unsupported(String),
}

impl std::fmt::Display for OpPlanError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            OpPlanError::Plan(detail) => write!(f, "invalid plan: {detail}"),
            OpPlanError::Action { op, detail } => write!(f, "invalid {op} action: {detail}"),
            OpPlanError::Unsupported(detail) => write!(f, "cannot execute the plan: {detail}"),
        }
    }
}

impl std::error::Error for OpPlanError {}

// ── The validated plan ──

#[derive(Debug, Clone, PartialEq)]
pub struct OpPlan {
    pub reply: String,
    pub actions: Vec<Action>,
    pub variants: Vec<Variant>,
    /// Skipped-unknown-op notes, appended to the chat reply by the caller.
    pub warnings: Vec<String>,
    /// True when the reply contained no JSON object at all (raw text = `reply`).
    pub chat_only: bool,
    /// The turn's question, when it asked one (contract §11).
    pub ask: Option<AskCard>,
}

/// One choice on an [`AskCard`] (contract §11). This server is a TOOL, not a chat: it
/// cannot show a picture, so an option's preview — a render spec or an image reference —
/// is dropped at parse time and only the label survives (§11.4). The option itself is
/// never dropped.
#[derive(Debug, Clone, PartialEq)]
pub struct AskOption {
    pub label: String,
}

/// A question the model puts back to the user (contract §11). There is no interactive
/// surface here, so the card is surfaced to the CALLING agent — as text on the reply and
/// as structured data — and answered by calling the tool again with the choice.
#[derive(Debug, Clone, PartialEq)]
pub struct AskCard {
    pub question: String,
    pub multi: bool,
    pub allow_custom: bool,
    pub custom_label: String,
    pub options: Vec<AskOption>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Variant {
    pub label: String,
    pub actions: Vec<Action>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Dir {
    Left,
    Right,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FilterMode {
    None,
    Bw,
    Sepia,
    Invert,
    Contour,
    Custom,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Axis {
    X,
    Y,
}

/// The §2 `formula` op's three forms.
#[derive(Debug, Clone, PartialEq)]
pub enum FormulaOp {
    /// `axis` + non-empty `expr` — set that axis's formula.
    Set { axis: Axis, expr: String },
    /// `axis` + empty `expr` — clear that axis.
    Clear { axis: Axis },
    /// `enabled` alone — switch formulas ON/OFF as a whole.
    Enable(bool),
}

/// The §2 page-size forms: an ISO format name, or custom cm dims.
#[derive(Debug, Clone, PartialEq)]
pub enum PageSize {
    Format(String),
    /// Custom dims in centimetres (each inclusive `DIM_CM_MIN..DIM_CM_MAX`).
    Cm { width: f64, height: f64 },
}

/// One validated action (contract §2). Field shapes match the wire, already cleaned:
/// `Frame` normalizes `index`/`indices` to one list.
#[derive(Debug, Clone, PartialEq)]
pub enum Action {
    Crop {
        x1: Option<String>,
        x2: Option<String>,
        y1: Option<String>,
        y2: Option<String>,
        /// Optional strict `W:H` ratio token, resolved by the CLI's core cropSpec.
        aspect: Option<String>,
    },
    Rotate {
        dir: Dir,
        times: u32,
    },
    Filter {
        mode: FilterMode,
        /// Present iff `mode` is `Custom` (`#rrggbb`).
        tint: Option<String>,
    },
    Layout {
        lines: Vec<Line>,
    },
    Formula(FormulaOp),
    Page {
        size: PageSize,
    },
    Blank {
        color: String,
        format: Option<String>,
        /// Optional explicit cm dims — both ride together and override `format`.
        dims_cm: Option<(f64, f64)>,
    },
    Frame {
        indices: Vec<u32>,
    },
    /// §2.1: switch the working image to the turn's Nth attachment (1-based).
    Image {
        index: u32,
    },
    /// §2.1: persist the current image + layout as a project (`<name>.stencil` here).
    /// `path` is the §10 destination — honored relative to `output_dir` at mapping time.
    Save {
        name: Option<String>,
        path: Option<String>,
    },
}

impl Action {
    /// The registry `"op"` this action came from — how a validated action finds its own
    /// §13 entry again (and with it the lowering that folds it into a run).
    pub fn op_name(&self) -> &'static str {
        match self {
            Action::Crop { .. } => "crop",
            Action::Rotate { .. } => "rotate",
            Action::Filter { .. } => "filter",
            Action::Layout { .. } => "layout",
            Action::Formula(_) => "formula",
            Action::Page { .. } => "page",
            Action::Blank { .. } => "blank",
            Action::Frame { .. } => "frame",
            Action::Image { .. } => "image",
            Action::Save { .. } => "save",
        }
    }
}
