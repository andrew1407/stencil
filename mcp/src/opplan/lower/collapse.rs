//! Collapsing one action list into a single [`EditParams`] in the CLI's fixed pipeline
//! order. Each action is folded by the lowering its §13 registry entry carries, so the ops
//! this stage executes are exactly the ops the validator accepted.
use crate::args::{Blank, Crop, EditParams, LayoutArg};
use crate::layout::Layout;
use crate::registry;

use super::super::fold::Fold;
use super::super::{Action, OpPlanError, PageSize};

/// A §2 cm dimension as CLI `--blank` pixels: `cm / 2.54 * 96` dpi, half-up, min 1 px.
fn cm_to_blank_px(cm: f64) -> u32 {
    ((cm / 2.54 * 96.0 + 0.5) as u32).max(1)
}

/// Collapse one action list into a single `EditParams`, each action folded by its §13
/// registry entry. Ops it cannot express in one run are rejected.
pub fn collapse<'a>(
    actions: impl IntoIterator<Item = &'a Action>,
    input: Option<&str>,
    output: String,
    output_dir: &str,
) -> Result<EditParams, OpPlanError> {
    let mut fold = Fold::default();
    for action in actions {
        let name = action.op_name();
        let descriptor = registry::descriptor(name).ok_or_else(|| {
            OpPlanError::Unsupported(format!("the `{name}` op has no lowering on this surface"))
        })?;
        (descriptor.lower)(action, &mut fold)?;
    }
    let Fold { crop, quarters, filter, lines, blank, page, frame } = fold;

    // A blank action replaces the input; a `page` action needs a blank to land on. The blank's
    // own size (dims beat format) wins over a `page` action's.
    let (params_input, params_blank) = match blank {
        Some((color, own)) => {
            let (page_format, width, height) = match own.or(page) {
                Some(PageSize::Format(format)) => (Some(format), None, None),
                Some(PageSize::Cm { width, height }) => {
                    (None, Some(cm_to_blank_px(width)), Some(cm_to_blank_px(height)))
                }
                None => (None, None, None),
            };
            (
                None,
                Some(Blank {
                    page: page_format,
                    width,
                    height,
                    color: Some(color),
                }),
            )
        }
        None => {
            if page.is_some() {
                return Err(OpPlanError::Unsupported(
                    "the `page` op needs a `blank` in the same plan — the CLI applies page \
                     formats only when creating a blank page"
                        .to_string(),
                ));
            }
            match input {
                Some(input) => (Some(input.to_string()), None),
                None => {
                    return Err(OpPlanError::Unsupported(
                        "the plan edits an image, but no `input` was given to \
                         stencil_prompt"
                            .to_string(),
                    ));
                }
            }
        }
    };

    // Net rotation, normalized to the CLI's smallest equivalent signed count.
    let rotate = match quarters.rem_euclid(4) {
        0 => None,
        3 => Some(-1),
        q => Some(q as i32),
    };

    let layout = if lines.is_empty() {
        None
    } else {
        Some(LayoutArg::Inline(Layout {
            image_width: None,
            image_height: None,
            filter: None,
            lines,
        }))
    };
    // Snapshot-frame plan coordinates ride with `--layout-frame source` so the CLI
    // re-maps them through the run's own crop/rotate (contract §1).
    let layout_frame = layout.is_some().then(|| "source".to_string());

    Ok(EditParams {
        input: params_input,
        blank: params_blank,
        frame,
        crop: crop.map(Crop::Spec),
        album: None,
        rotate,
        layout,
        layout_frame,
        filter,
        output,
        // Confined to output_dir, so the CLI refuses an escaping destination itself, behind
        // honored_save_path. Iterative prompting re-writes results: no clobber guard.
        confine_root: Some(output_dir.to_string()),
        overwrite: true,
        surface: None,
        server: None,
        remote_update: None,
        remote: None,
        remote_name: None,
    })
}
