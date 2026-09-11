//! Lowering (mapping) a validated plan onto CLI runs: splitting at `image`/`save`,
//! honoring §10 save paths inside `output_dir`, and collapsing each action list into one
//! [`EditParams`] in the CLI's fixed pipeline order.

use std::collections::HashSet;
use std::path::Path;

use crate::args::{Blank, Crop, EditParams, LayoutArg};
use crate::layout::Layout;

use super::fold::Fold;
use super::{Action, FormulaOp, OpPlan, OpPlanError, PageSize, MAX_LABEL_CHARS};
use crate::registry;

/// One CLI run derived from the plan: the base result (`label` = `None`,
/// `{output_dir}/result.png`), a variant (`label` = its sanitized name,
/// `{output_dir}/{label}.png`), or a §2.1 `save` (`project` = true, writing
/// `{output_dir}/{name}.stencil` — or a §10 `path` destination inside `output_dir`).
#[derive(Debug)]
pub struct EditRequest {
    pub label: Option<String>,
    /// True for a `save` op's `.stencil` project write (the CLI prints `wrote … (project)`).
    pub project: bool,
    pub params: EditParams,
}

/// Sanitize a variant label into a `[a-z0-9-]` file stem (runs of other characters become
/// single dashes; capped at 40 chars). May come out empty — callers fall back to a
/// positional `variant-N` name.
pub fn sanitize_label(label: &str) -> String {
    let mut out = String::new();
    for c in label.chars() {
        let c = c.to_ascii_lowercase();
        if c.is_ascii_lowercase() || c.is_ascii_digit() {
            if out.len() >= MAX_LABEL_CHARS {
                break;
            }
            out.push(c);
        } else if !out.is_empty() && !out.ends_with('-') {
            out.push('-');
        }
    }
    while out.ends_with('-') {
        out.pop();
    }
    out
}

/// Map a validated plan onto CLI runs: one for the base result when the plan's final
/// working image carries actions, one `.stencil` write per §2.1 `save`, plus one per
/// variant (which replays the base actions first — §1: variants branch from the state
/// *after* them). Plan coordinates are snapshot-frame, so drawing runs ride
/// `--layout-frame source`.
///
/// §2.1 multi-image plans: `stencil_prompt` carries a single `input`, so `image` index 1
/// restarts the working image from it and any higher index is an attachment this turn
/// cannot satisfy — that ACTION is skipped with a `notes` warning, never the whole plan.
/// Saving with no working image warns the same way.
pub fn to_edit_requests(
    plan: &OpPlan,
    input: Option<&str>,
    output_dir: &str,
    notes: &mut Vec<String>,
) -> Result<Vec<EditRequest>, OpPlanError> {
    let out_path = |stem: &str| {
        Path::new(output_dir)
            .join(format!("{stem}.png"))
            .to_string_lossy()
            .into_owned()
    };
    let project_path = |stem: &str| {
        Path::new(output_dir)
            .join(format!("{stem}.stencil"))
            .to_string_lossy()
            .into_owned()
    };

    // §2 widening: the formula clear/disable forms are valid but INERT here — a headless
    // CLI run starts with no formulas, so there is nothing to clear or switch off. One
    // note for the whole plan; the actions themselves are skipped below (and in collapse).
    if plan
        .actions
        .iter()
        .chain(plan.variants.iter().flat_map(|v| v.actions.iter()))
        .any(is_inert_formula)
    {
        notes.push(
            "Skipped the formula clear/disable — a headless CLI run starts with no formulas"
                .to_string(),
        );
    }

    // Walk the top-level actions, splitting them at each `image` switch: `current` is the
    // working image's actions, and every `save` snapshots them as one project write.
    let mut current: Vec<Action> = Vec::new();
    let mut saves: Vec<(Option<String>, Option<String>, Vec<Action>)> = Vec::new();
    for action in &plan.actions {
        match action {
            inert if is_inert_formula(inert) => {}
            Action::Image { index } => {
                if *index == 1 {
                    current.clear();
                } else {
                    notes.push(format!(
                        "Skipped switching to attached image {index} — stencil_prompt carries \
                         a single `input` image (index 1)"
                    ));
                }
            }
            Action::Save { name, path } => {
                if input.is_none() && !current.iter().any(|a| matches!(a, Action::Blank { .. })) {
                    notes.push("Skipped save — no working image to save".to_string());
                    continue;
                }
                saves.push((name.clone(), path.clone(), current.clone()));
            }
            other => current.push(other.clone()),
        }
    }

    let mut requests = Vec::new();
    let mut taken: HashSet<String> = HashSet::new();

    if !current.is_empty() {
        taken.insert("result".to_string());
        requests.push(EditRequest {
            label: None,
            project: false,
            params: collapse(&current, input, out_path("result"), output_dir)?,
        });
    }

    for (i, (name, path, actions)) in saves.iter().enumerate() {
        // §2.1: an unnamed save derives its name from the input, then a positional
        // fallback; colliding names stay distinct so one save never overwrites another.
        let mut stem = sanitize_label(name.as_deref().unwrap_or_default());
        if stem.is_empty() {
            stem = input.map(default_save_name).unwrap_or_default();
        }
        if stem.is_empty() {
            stem = format!("project-{}", i + 1);
        }
        stem = dedupe(stem, &mut taken);
        // §10 `path`: honored inside output_dir — this run's one sandbox. A destination
        // that cannot stay inside it saves to the usual place with a note instead.
        let output = match path.as_deref().map(|p| honored_save_path(p, output_dir, &stem)) {
            Some(Some(dest)) => dest,
            Some(None) => {
                notes.push(format!(
                    "Saved to the usual place — save path \"{}\" must be relative \
                     (inside output_dir, no \"..\" or \"~\")",
                    path.as_deref().unwrap_or_default()
                ));
                project_path(&stem)
            }
            None => project_path(&stem),
        };
        requests.push(EditRequest {
            label: Some(stem.clone()),
            project: true,
            params: collapse(actions, input, output, output_dir)?,
        });
    }

    for (i, variant) in plan.variants.iter().enumerate() {
        let mut stem = sanitize_label(&variant.label);
        if stem.is_empty() {
            stem = format!("variant-{}", i + 1);
        }
        // Keep colliding names distinct (two variants labelled "Rotated!" / "rotated").
        let stem = dedupe(stem, &mut taken);
        // A variant branches from the image AFTER the top-level actions, so its CLI run
        // replays those actions first — the FINAL working image's, in a multi-image plan.
        let params = collapse(
            current.iter().chain(&variant.actions),
            input,
            out_path(&stem),
            output_dir,
        )?;
        requests.push(EditRequest {
            label: Some(stem),
            project: false,
            params,
        });
    }

    Ok(requests)
}

/// The §2 formula forms this adapter ACCEPTS but cannot act on: clearing or toggling
/// formulas is a no-op in a headless run that starts with none (accepted-but-noted).
fn is_inert_formula(action: &Action) -> bool {
    matches!(
        action,
        Action::Formula(FormulaOp::Clear { .. } | FormulaOp::Enable(_))
    )
}

/// Claim `stem`, suffixing `-2`, `-3`, … until it is unique among the names already taken.
fn dedupe(stem: String, taken: &mut HashSet<String>) -> String {
    if taken.insert(stem.clone()) {
        return stem;
    }
    let mut n = 2;
    while !taken.insert(format!("{stem}-{n}")) {
        n += 1;
    }
    format!("{stem}-{n}")
}

/// Map a §10 save destination into the run's `output_dir`: a relative `.stencil` file
/// name is used as-is; any other relative path is a folder holding `{stem}.stencil`
/// (the CLI's folder-or-file rule). Absolute paths, `..` segments and `~` would escape
/// the sandbox — `None`, and the caller notes + saves to the usual place.
fn honored_save_path(path: &str, output_dir: &str, stem: &str) -> Option<String> {
    use std::path::Component;
    let p = Path::new(path);
    if p.is_absolute()
        || path.starts_with('~')
        || p.components().any(|c| !matches!(c, Component::Normal(_) | Component::CurDir))
    {
        return None;
    }
    let dest = if path.to_ascii_lowercase().ends_with(".stencil") {
        Path::new(output_dir).join(p)
    } else {
        Path::new(output_dir).join(p).join(format!("{stem}.stencil"))
    };
    Some(dest.to_string_lossy().into_owned())
}

/// The file stem an unnamed §2.1 `save` derives from the turn's input (path or URL),
/// sanitized into the same `[a-z0-9-]` shape variant labels use. May come out empty.
fn default_save_name(input: &str) -> String {
    let tail = input.rsplit(['/', '\\']).next().unwrap_or(input);
    let stem = tail.split(['?', '#']).next().unwrap_or(tail);
    let stem = stem.rsplit_once('.').map_or(stem, |(head, _)| head);
    sanitize_label(stem)
}

/// A §2 cm dimension as CLI `--blank` pixels: `cm / 2.54 * 96` dpi, half-up, min 1 px.
fn cm_to_blank_px(cm: f64) -> u32 {
    ((cm / 2.54 * 96.0 + 0.5) as u32).max(1)
}

/// Collapse one action list into a single `EditParams`. Each action is folded by the
/// lowering its §13 registry entry carries, so the ops this stage executes are exactly the
/// ops the validator accepted. Ops it cannot express in one run are rejected.
fn collapse<'a>(
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

    // Source: a blank action replaces the input; a page action needs a blank to land on
    // (the CLI applies page sizing only when creating a blank page). The blank's own
    // size (dims beat format) wins over a `page` action's; cm dims become the CLI's
    // pixel dims via the core's defaultBlankSizePx conversion.
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
