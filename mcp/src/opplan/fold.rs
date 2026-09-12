//! Folding one action into the single CLI run its plan collapses to — the second half of
//! a §13 registry entry. [`crate::registry::OpDescriptor`] pairs each fold with the key
//! schema that validated the action, so validation and dispatch come off one table (the
//! shape pystencil's `OpSpec` carries as `validator` + `applier`).
//!
//! Every fold takes the action it was registered for; handed anything else it does
//! nothing, so the table is the only thing that has to be right.

use crate::layout::Line;

use super::{Action, Dir, FilterMode, FormulaOp, OpPlanError, PageSize};

/// One fold: the registered action, folded into the run being accumulated.
pub type Lower = fn(&Action, &mut Fold) -> Result<(), OpPlanError>;

/// The CLI run taking shape: the pipeline order is fixed (source → frame → crop → rotate
/// → filter → layout), so rotations sum, layout lines concatenate, and the last filter wins.
#[derive(Default)]
pub struct Fold {
    pub crop: Option<String>,
    pub quarters: i64,
    pub filter: Option<String>,
    pub lines: Vec<Line>,
    pub blank: Option<(String, Option<PageSize>)>,
    pub page: Option<PageSize>,
    pub frame: Option<u32>,
}

pub fn crop(action: &Action, fold: &mut Fold) -> Result<(), OpPlanError> {
    let Action::Crop { x1, x2, y1, y2, aspect } = action else {
        return Ok(());
    };
    if fold.crop.is_some() {
        return Err(OpPlanError::Unsupported(
            "two crop actions cannot be combined into one CLI run — use one \
             crop per image/variant"
                .to_string(),
        ));
    }
    let spec = [("x1", x1), ("x2", x2), ("y1", y1), ("y2", y2), ("aspect", aspect)]
        .iter()
        .filter_map(|(name, edge)| edge.as_ref().map(|v| format!("{name}={v}")))
        .collect::<Vec<_>>()
        .join(" ");
    fold.crop = Some(spec);
    Ok(())
}

pub fn rotate(action: &Action, fold: &mut Fold) -> Result<(), OpPlanError> {
    let Action::Rotate { dir, times } = action else {
        return Ok(());
    };
    // CLI `-r n` is n quarter-turns clockwise; `right` is clockwise.
    fold.quarters += match dir {
        Dir::Right => i64::from(*times),
        Dir::Left => -i64::from(*times),
    };
    Ok(())
}

pub fn filter(action: &Action, fold: &mut Fold) -> Result<(), OpPlanError> {
    let Action::Filter { mode, tint } = action else {
        return Ok(());
    };
    fold.filter = match mode {
        FilterMode::None => None,
        FilterMode::Bw => Some("bw".to_string()),
        FilterMode::Sepia => Some("sepia".to_string()),
        FilterMode::Invert => Some("invert".to_string()),
        FilterMode::Contour => Some("contour".to_string()),
        // `custom` ⇒ pass the tint color as the CLI --filter value.
        FilterMode::Custom => tint.clone(),
    };
    Ok(())
}

pub fn layout(action: &Action, fold: &mut Fold) -> Result<(), OpPlanError> {
    if let Action::Layout { lines } = action {
        fold.lines.extend(lines.iter().cloned());
    }
    Ok(())
}

pub fn formula(action: &Action, _fold: &mut Fold) -> Result<(), OpPlanError> {
    // The clear/disable forms are inert — `to_edit_requests` already noted them.
    if !matches!(action, Action::Formula(FormulaOp::Set { .. })) {
        return Ok(());
    }
    Err(OpPlanError::Unsupported(
        "the `formula` op cannot run headlessly here — the stencil CLI has no \
         formula flag (use the browser or desktop editor for formulas)"
            .to_string(),
    ))
}

pub fn page(action: &Action, fold: &mut Fold) -> Result<(), OpPlanError> {
    if let Action::Page { size } = action {
        fold.page = Some(size.clone());
    }
    Ok(())
}

pub fn blank(action: &Action, fold: &mut Fold) -> Result<(), OpPlanError> {
    let Action::Blank { color, format, dims_cm } = action else {
        return Ok(());
    };
    // §2: the blank's own explicit cm dims override its format.
    let own = dims_cm
        .map(|(width, height)| PageSize::Cm { width, height })
        .or_else(|| format.clone().map(PageSize::Format));
    fold.blank = Some((color.clone(), own));
    Ok(())
}

pub fn frame(action: &Action, fold: &mut Fold) -> Result<(), OpPlanError> {
    let Action::Frame { indices } = action else {
        return Ok(());
    };
    if indices.len() != 1 {
        return Err(OpPlanError::Unsupported(
            "multiple frame indices cannot be combined into one CLI run — ask \
             for one frame, or one variant per frame"
                .to_string(),
        ));
    }
    if fold.frame.is_some() {
        return Err(OpPlanError::Unsupported(
            "two frame actions cannot be combined into one CLI run".to_string(),
        ));
    }
    fold.frame = Some(indices[0]);
    Ok(())
}

/// §2.1 ops never reach a single run: `to_edit_requests` splits the plan at each `image`
/// switch and turns every `save` into its own `.stencil` write.
pub fn split(_action: &Action, _fold: &mut Fold) -> Result<(), OpPlanError> {
    Ok(())
}
