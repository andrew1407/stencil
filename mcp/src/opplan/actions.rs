//! Per-op action validation (contract §2–§3): the registry-gated dispatch, one validator
//! per op, and the token/shape helpers they share.

use serde_json::Value;

use crate::layout::{Line, Point};
use crate::registry;

use super::{
    Action, Axis, Dir, FilterMode, FormulaOp, OpPlanError, PageSize, DIM_CM_MAX, DIM_CM_MIN,
    MAX_ACTIONS, MAX_FRAME_INDICES, MAX_LAYOUT_LINES, MAX_PATH_CHARS, MAX_SAVE_NAME,
    MAX_STRING_CHARS,
};

/// The first top-level-only op named in a raw actions list (§2.1), if any. Callers use it
/// to drop the offending variant / ask preview per §1 — misplaced ops never reach the
/// validation below.
pub(super) fn misplaced_top_level_op(value: Option<&Value>) -> Option<String> {
    let Some(Value::Array(list)) = value else {
        return None;
    };
    list.iter()
        .filter_map(|raw| raw.get("op").and_then(Value::as_str))
        .find(|op| registry::descriptor(op).is_some_and(|d| d.top_level_only))
        .map(str::to_string)
}

/// Validate one actions list: unknown ops drop with a warning (forward compatibility);
/// a known op with invalid params fails the whole plan.
pub(super) fn validate_actions(
    value: Option<&Value>,
    warnings: &mut Vec<String>,
    where_: &str,
) -> Result<Vec<Action>, OpPlanError> {
    let list = match value {
        None | Some(Value::Null) => return Ok(Vec::new()),
        Some(Value::Array(list)) => list,
        Some(_) => {
            return Err(OpPlanError::Plan(format!("{where_} must be an array")));
        }
    };
    if list.len() > MAX_ACTIONS {
        return Err(OpPlanError::Plan(format!(
            "more than {MAX_ACTIONS} actions in {where_}"
        )));
    }
    let mut out = Vec::new();
    for raw in list {
        let (Value::Object(action), Some(op)) = (raw, raw.get("op").and_then(Value::as_str))
        else {
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
        match descriptor.name {
            "crop" => out.push(validate_crop(action)?),
            "rotate" => out.push(validate_rotate(action)?),
            "filter" => out.push(validate_filter(action)?),
            "layout" => out.push(validate_layout(action)?),
            "formula" => out.push(validate_formula(action)?),
            "page" => out.push(validate_page(action)?),
            "blank" => out.push(validate_blank(action)?),
            "frame" => out.push(validate_frame(action)?),
            "image" => out.push(validate_image(action)?),
            "save" => out.push(validate_save(action)?),
            // A registry entry without a validator arm is a wiring mistake — fail loudly
            // (the registry cross-check test catches this before it ships).
            other => {
                return Err(OpPlanError::Plan(format!(
                    "registered op \"{other}\" has no validator — registry/validator mismatch"
                )));
            }
        }
    }
    Ok(out)
}

type JsonObject = serde_json::Map<String, Value>;

fn fail(op: &str, detail: impl Into<String>) -> OpPlanError {
    OpPlanError::Action {
        op: op.to_string(),
        detail: detail.into(),
    }
}

/// Reject unknown fields on a KNOWN op — that fails the whole plan (contract §1).
fn only_keys(action: &JsonObject, op: &str, allowed: &[&str]) -> Result<(), OpPlanError> {
    for key in action.keys() {
        if key != "op" && !allowed.contains(&key.as_str()) {
            return Err(fail(op, format!("unknown field \"{key}\"")));
        }
    }
    Ok(())
}

/// A string within the contract's per-field length limit.
fn as_bounded_str(value: &Value) -> Option<&str> {
    value.as_str().filter(|s| s.len() <= MAX_STRING_CHARS)
}

/// A cropSpec token: optional `-`, a number (`12`, `1.5`, `.5`), optional `%`/`px`/`cm`/`in`.
fn is_crop_token(token: &str) -> bool {
    let rest = token.strip_prefix('-').unwrap_or(token);
    let unit_start = rest
        .find(|c: char| !c.is_ascii_digit() && c != '.')
        .unwrap_or(rest.len());
    let (number, unit) = rest.split_at(unit_start);
    let valid_number = match number.split_once('.') {
        None => !number.is_empty() && number.bytes().all(|b| b.is_ascii_digit()),
        Some((int, frac)) => {
            !frac.is_empty()
                && frac.bytes().all(|b| b.is_ascii_digit())
                && int.bytes().all(|b| b.is_ascii_digit())
        }
    };
    valid_number && matches!(unit, "" | "%" | "px" | "cm" | "in")
}

/// A crop `aspect` token: strict `W:H`, digits only, both positive — the same rule
/// core/parse/cropSpec.cpp enforces at resolution time.
fn is_aspect_token(token: &str) -> bool {
    let Some((w, h)) = token.split_once(':') else {
        return false;
    };
    let positive_int = |s: &str| {
        !s.is_empty() && s.bytes().all(|b| b.is_ascii_digit()) && s.bytes().any(|b| b != b'0')
    };
    positive_int(w) && positive_int(h)
}

/// A lowercase ISO page name: `a0`–`a10`, `b0`–`b10`, `c0`–`c10` (contract §2 — lowercase).
fn is_page_format(format: &str) -> bool {
    let mut chars = format.chars();
    let Some(series) = chars.next() else {
        return false;
    };
    let number = chars.as_str();
    matches!(series, 'a' | 'b' | 'c')
        && (number == "10" || (number.len() == 1 && number.bytes().all(|b| b.is_ascii_digit())))
}

/// `#` + exactly 6 hex digits.
fn is_hex6(color: &str) -> bool {
    color
        .strip_prefix('#')
        .is_some_and(|hex| hex.len() == 6 && hex.bytes().all(|b| b.is_ascii_hexdigit()))
}

fn validate_crop(action: &JsonObject) -> Result<Action, OpPlanError> {
    only_keys(action, "crop", &["spec", "aspect"])?;
    // §1 tolerance: "aspect" beside "spec" is validated the same way and folded into the
    // spec when the spec lacks it; a conflicting duplicate fails the plan.
    let beside = match action.get("aspect") {
        None => None,
        Some(value) => match as_bounded_str(value) {
            Some(token) if is_aspect_token(token) => Some(token.to_string()),
            _ => return Err(fail("crop", "bad token for \"aspect\"")),
        },
    };
    let Some(Value::Object(spec)) = action.get("spec") else {
        return Err(fail("crop", "\"spec\" must be an object"));
    };
    for key in spec.keys() {
        if !matches!(key.as_str(), "x1" | "x2" | "y1" | "y2" | "aspect") {
            return Err(fail("crop", format!("unknown spec key \"{key}\"")));
        }
    }
    if spec.is_empty() && beside.is_none() {
        return Err(fail("crop", "spec needs at least one of x1/x2/y1/y2/aspect"));
    }
    let edge = |key: &str| -> Result<Option<String>, OpPlanError> {
        match spec.get(key) {
            None => Ok(None),
            Some(value) => match as_bounded_str(value) {
                Some(token) if is_crop_token(token) => Ok(Some(token.to_string())),
                _ => Err(fail("crop", format!("bad token for \"{key}\""))),
            },
        }
    };
    let aspect = match spec.get("aspect") {
        None => None,
        Some(value) => match as_bounded_str(value) {
            Some(token) if is_aspect_token(token) => Some(token.to_string()),
            _ => return Err(fail("crop", "bad token for \"aspect\"")),
        },
    };
    let aspect = match (aspect, beside) {
        (Some(inside), Some(beside)) if inside != beside => {
            return Err(fail(
                "crop",
                "conflicting \"aspect\" inside and beside \"spec\"",
            ));
        }
        (inside, beside) => inside.or(beside),
    };
    Ok(Action::Crop {
        x1: edge("x1")?,
        x2: edge("x2")?,
        y1: edge("y1")?,
        y2: edge("y2")?,
        aspect,
    })
}

fn validate_rotate(action: &JsonObject) -> Result<Action, OpPlanError> {
    only_keys(action, "rotate", &["dir", "times"])?;
    let dir = match action.get("dir").and_then(Value::as_str) {
        Some("left") => Dir::Left,
        Some("right") => Dir::Right,
        _ => return Err(fail("rotate", "\"dir\" must be \"left\" or \"right\"")),
    };
    let times = match action.get("times") {
        None | Some(Value::Null) => 1,
        Some(value) => match value.as_u64() {
            Some(t @ 1..=3) => t as u32,
            _ => return Err(fail("rotate", "\"times\" must be an integer 1..3")),
        },
    };
    Ok(Action::Rotate { dir, times })
}

fn validate_filter(action: &JsonObject) -> Result<Action, OpPlanError> {
    only_keys(action, "filter", &["mode", "tint"])?;
    let mode = match action.get("mode").and_then(Value::as_str) {
        Some("none") => FilterMode::None,
        Some("bw") => FilterMode::Bw,
        Some("sepia") => FilterMode::Sepia,
        Some("invert") => FilterMode::Invert,
        Some("contour") => FilterMode::Contour,
        Some("custom") => FilterMode::Custom,
        other => {
            return Err(fail(
                "filter",
                format!("unknown mode {:?}", other.unwrap_or("<missing>")),
            ));
        }
    };
    let tint = action.get("tint").filter(|v| !v.is_null());
    if mode == FilterMode::Custom {
        let Some(tint) = tint.and_then(as_bounded_str).filter(|t| is_hex6(t)) else {
            return Err(fail("filter", "\"custom\" requires \"tint\" as #rrggbb"));
        };
        return Ok(Action::Filter {
            mode,
            tint: Some(tint.to_string()),
        });
    }
    if tint.is_some() {
        return Err(fail("filter", "\"tint\" is only valid with mode \"custom\""));
    }
    Ok(Action::Filter { mode, tint: None })
}

fn validate_layout(action: &JsonObject) -> Result<Action, OpPlanError> {
    only_keys(action, "layout", &["lines"])?;
    let Some(Value::Array(raw_lines)) = action.get("lines") else {
        return Err(fail("layout", "\"lines\" must be an array"));
    };
    if raw_lines.len() > MAX_LAYOUT_LINES {
        return Err(fail("layout", format!("more than {MAX_LAYOUT_LINES} lines")));
    }
    let mut lines = Vec::with_capacity(raw_lines.len());
    for raw in raw_lines {
        lines.push(validate_line(raw)?);
    }
    Ok(Action::Layout { lines })
}

/// Validate one layout Line per contract §3, producing the shared `layout::Line` type the
/// pipeline already serializes for the CLI's `--layout`.
fn validate_line(raw: &Value) -> Result<Line, OpPlanError> {
    let Value::Object(line) = raw else {
        return Err(fail("layout", "each line must be an object"));
    };
    const LINE_KEYS: [&str; 7] = [
        "points",
        "color",
        "thickness",
        "pointSize",
        "style",
        "locked",
        "fillColor",
    ];
    for key in line.keys() {
        if !LINE_KEYS.contains(&key.as_str()) {
            return Err(fail("layout", format!("unknown line field \"{key}\"")));
        }
    }
    let Some(Value::Array(raw_points)) = line.get("points") else {
        return Err(fail("layout", "line \"points\" must be an array"));
    };
    let mut points = Vec::with_capacity(raw_points.len());
    for point in raw_points {
        let Value::Object(p) = point else {
            return Err(fail("layout", "points must be {x, y} objects"));
        };
        if p.keys().any(|k| k != "x" && k != "y") {
            return Err(fail("layout", "points must be {x, y} objects"));
        }
        let (Some(x), Some(y)) = (
            p.get("x").and_then(Value::as_f64),
            p.get("y").and_then(Value::as_f64),
        ) else {
            return Err(fail("layout", "point coords must be finite numbers"));
        };
        points.push(Point { x, y });
    }

    let string_field = |key: &str, what: &str| -> Result<Option<String>, OpPlanError> {
        match line.get(key) {
            None | Some(Value::Null) => Ok(None),
            Some(value) => match as_bounded_str(value) {
                Some(s) => Ok(Some(s.to_string())),
                None => Err(fail("layout", format!("line \"{what}\" must be a string"))),
            },
        }
    };
    let number_field = |key: &str, what: &str| -> Result<Option<f64>, OpPlanError> {
        match line.get(key) {
            None | Some(Value::Null) => Ok(None),
            Some(value) => match value.as_f64() {
                Some(n) => Ok(Some(n)),
                None => Err(fail("layout", format!("line \"{what}\" must be a number"))),
            },
        }
    };

    let style = match line.get("style") {
        None | Some(Value::Null) => None,
        Some(value) => match value.as_str() {
            Some(s @ ("solid" | "dashed" | "dotted")) => Some(s.to_string()),
            other => {
                return Err(fail(
                    "layout",
                    format!("unknown line style {:?}", other.unwrap_or("<not a string>")),
                ));
            }
        },
    };
    let locked = match line.get("locked") {
        None | Some(Value::Null) => None,
        Some(Value::Bool(b)) => Some(*b),
        Some(_) => return Err(fail("layout", "line \"locked\" must be a boolean")),
    };

    Ok(Line {
        points,
        color: string_field("color", "color")?,
        thickness: number_field("thickness", "thickness")?,
        point_size: number_field("pointSize", "pointSize")?,
        style,
        locked,
        fill_color: string_field("fillColor", "fillColor")?,
        // `pointColor` is an editor control, not part of contract §3 — deliberately not
        // read; `None` leaves the points inheriting `color`.
        point_color: None,
    })
}

fn validate_formula(action: &JsonObject) -> Result<Action, OpPlanError> {
    only_keys(action, "formula", &["axis", "expr", "enabled"])?;
    let present = |key: &str| !matches!(action.get(key), None | Some(Value::Null));
    // §2: `enabled` rides ALONE — false switches formulas off (restoring identity).
    if present("enabled") {
        if present("axis") || present("expr") {
            return Err(fail(
                "formula",
                "\"enabled\" rides alone — use axis+expr OR enabled",
            ));
        }
        let Some(enabled) = action.get("enabled").and_then(Value::as_bool) else {
            return Err(fail("formula", "\"enabled\" must be a boolean"));
        };
        return Ok(Action::Formula(FormulaOp::Enable(enabled)));
    }
    let axis = match action.get("axis").and_then(Value::as_str) {
        Some("x") => Axis::X,
        Some("y") => Axis::Y,
        _ => return Err(fail("formula", "\"axis\" must be \"x\" or \"y\"")),
    };
    let Some(expr) = action.get("expr").and_then(as_bounded_str) else {
        return Err(fail("formula", "\"expr\" must be a string (empty clears that axis)"));
    };
    // §2: an empty (or whitespace) expr clears that axis.
    if expr.trim().is_empty() {
        return Ok(Action::Formula(FormulaOp::Clear { axis }));
    }
    // Charset check with the single variable matching the axis; the core formula engine
    // validates the expression again before use (contract §2).
    let variable = axis.as_char();
    let allowed = |c: char| c.is_ascii_digit() || "+-*/(). ".contains(c) || c == variable;
    if !expr.chars().all(allowed) {
        return Err(fail(
            "formula",
            format!("\"expr\" may only use digits, + - * / ( ) . and \"{variable}\""),
        ));
    }
    Ok(Action::Formula(FormulaOp::Set {
        axis,
        expr: expr.to_string(),
    }))
}

/// A required §2 cm dimension: a finite number, inclusive `DIM_CM_MIN..DIM_CM_MAX`.
fn cm_dim(action: &JsonObject, op: &str, key: &str) -> Result<f64, OpPlanError> {
    match action.get(key).and_then(Value::as_f64) {
        Some(cm) if (DIM_CM_MIN..=DIM_CM_MAX).contains(&cm) => Ok(cm),
        _ => Err(fail(
            op,
            format!("\"{key}\" must be centimetres {DIM_CM_MIN}..{DIM_CM_MAX}"),
        )),
    }
}

fn validate_page(action: &JsonObject) -> Result<Action, OpPlanError> {
    only_keys(action, "page", &["format", "width", "height"])?;
    let present = |key: &str| !matches!(action.get(key), None | Some(Value::Null));
    // §2: exactly one form — an ISO format, or custom width+height in cm.
    let has_format = present("format");
    let has_dims = present("width") || present("height");
    if has_format == has_dims {
        return Err(fail(
            "page",
            "exactly one of \"format\" / \"width\"+\"height\" is required",
        ));
    }
    if has_format {
        return match action.get("format").and_then(as_bounded_str) {
            Some(format) if is_page_format(format) => Ok(Action::Page {
                size: PageSize::Format(format.to_string()),
            }),
            _ => Err(fail(
                "page",
                "\"format\" must be a lowercase ISO name a0–a10, b0–b10 or c0–c10",
            )),
        };
    }
    Ok(Action::Page {
        size: PageSize::Cm {
            width: cm_dim(action, "page", "width")?,
            height: cm_dim(action, "page", "height")?,
        },
    })
}

fn validate_blank(action: &JsonObject) -> Result<Action, OpPlanError> {
    only_keys(action, "blank", &["color", "format", "width", "height"])?;
    let color = match action.get("color").and_then(as_bounded_str) {
        Some(color)
            if is_hex6(color)
                || (!color.is_empty() && color.bytes().all(|b| b.is_ascii_alphabetic())) =>
        {
            color.to_string()
        }
        _ => return Err(fail("blank", "\"color\" must be #rrggbb or a CSS color name")),
    };
    let format = match action.get("format") {
        None | Some(Value::Null) => None,
        Some(value) => match as_bounded_str(value) {
            Some(format) if is_page_format(format) => Some(format.to_string()),
            _ => return Err(fail("blank", "bad page \"format\"")),
        },
    };
    // §2: explicit cm dims ride together and override the format.
    let present = |key: &str| !matches!(action.get(key), None | Some(Value::Null));
    if present("width") != present("height") {
        return Err(fail("blank", "\"width\" and \"height\" ride together"));
    }
    let dims_cm = if present("width") {
        Some((
            cm_dim(action, "blank", "width")?,
            cm_dim(action, "blank", "height")?,
        ))
    } else {
        None
    };
    Ok(Action::Blank {
        color,
        format,
        dims_cm,
    })
}

fn validate_frame(action: &JsonObject) -> Result<Action, OpPlanError> {
    only_keys(action, "frame", &["index", "indices"])?;
    let index = action.get("index").filter(|v| !v.is_null());
    let indices = action.get("indices").filter(|v| !v.is_null());
    match (index, indices) {
        (Some(_), Some(_)) | (None, None) => Err(fail(
            "frame",
            "exactly one of \"index\" / \"indices\" is required",
        )),
        (Some(value), None) => match value.as_u64().and_then(|i| u32::try_from(i).ok()) {
            Some(index) => Ok(Action::Frame {
                indices: vec![index],
            }),
            None => Err(fail("frame", "\"index\" must be an integer >= 0")),
        },
        (None, Some(value)) => {
            let Value::Array(list) = value else {
                return Err(fail("frame", "\"indices\" must be a non-empty array"));
            };
            if list.is_empty() {
                return Err(fail("frame", "\"indices\" must be a non-empty array"));
            }
            if list.len() > MAX_FRAME_INDICES {
                return Err(fail(
                    "frame",
                    format!("more than {MAX_FRAME_INDICES} indices"),
                ));
            }
            let mut indices = Vec::with_capacity(list.len());
            for item in list {
                match item.as_u64().and_then(|i| u32::try_from(i).ok()) {
                    Some(index) => indices.push(index),
                    None => return Err(fail("frame", "indices must be integers >= 0")),
                }
            }
            Ok(Action::Frame { indices })
        }
    }
}

/// §2.1 `image`: a 1-based index into the images the turn attached.
fn validate_image(action: &JsonObject) -> Result<Action, OpPlanError> {
    only_keys(action, "image", &["index"])?;
    match action.get("index").and_then(Value::as_u64) {
        Some(index @ 1..) => Ok(Action::Image {
            index: u32::try_from(index).map_err(|_| fail("image", "\"index\" is out of range"))?,
        }),
        _ => Err(fail("image", "\"index\" must be an integer >= 1")),
    }
}

/// §2.1 `save`: an optional project name (≤ 120 chars) and an optional §10 destination
/// path (≤ 1024 chars, trimmed, never a URL; empty after trim ≡ absent).
fn validate_save(action: &JsonObject) -> Result<Action, OpPlanError> {
    only_keys(action, "save", &["name", "path"])?;
    let name = match action.get("name") {
        None | Some(Value::Null) => None,
        Some(Value::String(name)) if name.chars().count() <= MAX_SAVE_NAME => Some(name.clone()),
        Some(_) => {
            return Err(fail(
                "save",
                format!("\"name\" must be a string of at most {MAX_SAVE_NAME} characters"),
            ))
        }
    };
    let path = match action.get("path") {
        None | Some(Value::Null) => None,
        Some(Value::String(path)) if path.chars().count() <= MAX_PATH_CHARS => {
            let path = path.trim();
            if has_url_scheme(path) {
                return Err(fail("save", "\"path\" is a local path, not a URL"));
            }
            (!path.is_empty()).then(|| path.to_string())
        }
        Some(_) => {
            return Err(fail(
                "save",
                format!("\"path\" must be a string of at most {MAX_PATH_CHARS} characters"),
            ))
        }
    };
    Ok(Action::Save { name, path })
}

/// The shared save-path shape check: a `scheme://` prefix (`^[A-Za-z][A-Za-z0-9+.-]*://`).
fn has_url_scheme(path: &str) -> bool {
    let Some(pos) = path.find("://") else {
        return false;
    };
    let mut chars = path[..pos].chars();
    matches!(chars.next(), Some(c) if c.is_ascii_alphabetic())
        && chars.all(|c| c.is_ascii_alphanumeric() || matches!(c, '+' | '.' | '-'))
}
