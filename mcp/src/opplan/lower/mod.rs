//! Lowering (mapping) a validated plan onto CLI runs: splitting at `image`/`save`,
//! honoring §10 save paths inside `output_dir`, and collapsing each action list into one
//! [`EditParams`] in the CLI's fixed pipeline order (collapse.rs).

mod collapse;
mod names;

use std::collections::HashSet;
use std::path::Path;

use crate::args::EditParams;

use super::{Action, FormulaOp, OpPlan, OpPlanError};

use collapse::collapse;
pub use names::sanitize_label;
use names::{dedupe, default_save_name, honored_save_path};

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
