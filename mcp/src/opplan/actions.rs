//! Per-op action handling (contract §2–§3): the registry-gated dispatch, the table-driven
//! check (`schema.rs`), and the typed normalizers that fill an [`Action`] from the
//! already-validated, defaults-applied values.

use serde_json::Value;

use crate::layout::Line;
use crate::registry;

use super::schema::{schema, JsonObject};
use super::{Action, Axis, Dir, FilterMode, FormulaOp, OpPlanError, PageSize};

/// The first top-level-only op named in a raw actions list (§2.1), if any. Callers use it
/// to drop the offending variant / ask preview per §1 — misplaced ops never reach the
/// validation below.
pub(super) fn misplaced_top_level_op(value: Option<&Value>) -> Option<String> {
    let Some(Value::Array(list)) = value else {
        return None;
    };
    list.iter()
        .filter_map(|raw| raw.get("op").and_then(Value::as_str))
        .find(|op| schema().entry(op).is_some_and(|e| e.flag("topLevelOnly")))
        .map(str::to_string)
}

/// Validate one actions list: unknown ops drop with a warning (forward compatibility);
/// a known op with invalid params fails the whole plan. `where_` names the list in
/// messages (`actions`, `variant 2 actions`, `ask option 1 actions`).
pub(super) fn validate_actions(
    value: Option<&Value>,
    warnings: &mut Vec<String>,
    where_: &str,
) -> Result<Vec<Action>, OpPlanError> {
    let list = match value {
        None | Some(Value::Null) => return Ok(Vec::new()),
        Some(value) => {
            schema()
                .check_envelope(value, "actions", where_)
                .map_err(OpPlanError::Plan)?;
            value.as_array().expect("checked as an array")
        }
    };
    let mut out = Vec::new();
    for raw in list {
        let (Value::Object(action), Some(op)) = (raw, raw.get("op").and_then(Value::as_str)) else {
            return Err(OpPlanError::Plan(format!(
                "every action in {where_} must be an object with an \"op\""
            )));
        };
        // §13: forbidden ops are boundaries, not unknown ops — a plan naming one fails
        // hard instead of falling to the forward-compatibility skip.
        if registry::is_forbidden(op) {
            return Err(OpPlanError::Plan(format!(
                "the \"{op}\" op is never model-drivable (llm-contract.md §13) — refused"
            )));
        }
        // The registry is the single known-ness gate (§13): an op with no active entry is
        // unknown here AND absent from the generated prompt, by construction.
        let Some(descriptor) = registry::descriptor(op) else {
            warnings.push(format!("Skipped unknown operation \"{op}\""));
            continue;
        };
        let entry = schema()
            .entry(descriptor.name)
            .expect("registered ops have a schema entry");
        let checked = schema()
            .validate_action(action, entry)
            .map_err(|detail| fail(op, detail))?;
        out.push(lower(&schema().normalize(&checked, entry))?);
    }
    Ok(out)
}

fn fail(op: &str, detail: impl Into<String>) -> OpPlanError {
    OpPlanError::Action {
        op: op.to_string(),
        detail: detail.into(),
    }
}

// ── typed normalizers: a validated, normalized action value → `Action` ──

fn str_of<'a>(v: &'a JsonObject, key: &str) -> Option<&'a str> {
    v.get(key).and_then(Value::as_str)
}

fn owned(v: &JsonObject, key: &str) -> Option<String> {
    str_of(v, key).map(str::to_string)
}

fn num_of(v: &JsonObject, key: &str) -> Option<f64> {
    v.get(key).and_then(Value::as_f64)
}

/// A validated non-negative integer as `u32`; `None` past `u32::MAX`.
fn u32_of(v: &Value) -> Option<u32> {
    v.as_f64()
        .filter(|f| f.fract() == 0.0 && *f >= 0.0 && *f <= f64::from(u32::MAX))
        .map(|f| f as u32)
}

/// A key the schema guarantees; its absence is a registry/normalizer mismatch.
fn need<T>(op: &str, key: &str, value: Option<T>) -> Result<T, OpPlanError> {
    value.ok_or_else(|| fail(op, format!("\"{key}\" is required")))
}

fn need_u32(op: &str, key: &str, value: Option<&Value>) -> Result<u32, OpPlanError> {
    value
        .and_then(u32_of)
        .ok_or_else(|| fail(op, format!("\"{key}\" is out of range")))
}

fn lower(v: &JsonObject) -> Result<Action, OpPlanError> {
    let op = str_of(v, "op").unwrap_or_default();
    Ok(match op {
        "crop" => {
            let spec = v
                .get("spec")
                .and_then(Value::as_object)
                .cloned()
                .unwrap_or_default();
            Action::Crop {
                x1: owned(&spec, "x1"),
                x2: owned(&spec, "x2"),
                y1: owned(&spec, "y1"),
                y2: owned(&spec, "y2"),
                aspect: owned(&spec, "aspect"),
            }
        }
        "rotate" => Action::Rotate {
            dir: match need(op, "dir", str_of(v, "dir"))? {
                "left" => Dir::Left,
                _ => Dir::Right,
            },
            times: need(op, "times", num_of(v, "times"))? as u32,
        },
        "filter" => Action::Filter {
            mode: match need(op, "mode", str_of(v, "mode"))? {
                "none" => FilterMode::None,
                "bw" => FilterMode::Bw,
                "sepia" => FilterMode::Sepia,
                "invert" => FilterMode::Invert,
                "contour" => FilterMode::Contour,
                _ => FilterMode::Custom,
            },
            tint: owned(v, "tint"),
        },
        // The normalized lines carry only the registry's line fields, so the strict
        // serde `Line` fills without `pointColor` (an editor control, not contract §3).
        "layout" => Action::Layout {
            lines: serde_json::from_value::<Vec<Line>>(
                v.get("lines").cloned().unwrap_or(Value::Null),
            )
            .map_err(|e| fail(op, e.to_string()))?,
        },
        "formula" => Action::Formula(match v.get("enabled").and_then(Value::as_bool) {
            Some(enabled) => FormulaOp::Enable(enabled),
            None => {
                let axis = match need(op, "axis", str_of(v, "axis"))? {
                    "x" => Axis::X,
                    _ => Axis::Y,
                };
                let expr = need(op, "expr", str_of(v, "expr"))?;
                // §2: an empty (or whitespace) expr clears that axis.
                if expr.trim().is_empty() {
                    FormulaOp::Clear { axis }
                } else {
                    FormulaOp::Set {
                        axis,
                        expr: expr.to_string(),
                    }
                }
            }
        }),
        "page" => Action::Page {
            size: match owned(v, "format") {
                Some(format) => PageSize::Format(format),
                None => PageSize::Cm {
                    width: need(op, "width", num_of(v, "width"))?,
                    height: need(op, "height", num_of(v, "height"))?,
                },
            },
        },
        "blank" => Action::Blank {
            color: need(op, "color", owned(v, "color"))?,
            format: owned(v, "format"),
            dims_cm: num_of(v, "width").zip(num_of(v, "height")),
        },
        "frame" => Action::Frame {
            indices: match v.get("indices").and_then(Value::as_array) {
                Some(list) => list
                    .iter()
                    .map(|i| need_u32(op, "indices", Some(i)))
                    .collect::<Result<Vec<_>, _>>()?,
                None => vec![need_u32(op, "index", v.get("index"))?],
            },
        },
        "image" => Action::Image {
            index: need_u32(op, "index", v.get("index"))?,
        },
        // `path` arrives trimmed (registry `trim`); empty after trim ≡ absent.
        "save" => Action::Save {
            name: owned(v, "name"),
            path: owned(v, "path").filter(|p| !p.is_empty()),
        },
        // A registry entry without a normalizer arm is a wiring mistake — fail loudly
        // (the registry cross-check test catches this before it ships).
        other => {
            return Err(OpPlanError::Plan(format!(
                "registered op \"{other}\" has no normalizer — registry/normalizer mismatch"
            )));
        }
    })
}
