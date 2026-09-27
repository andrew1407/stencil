//! A typed plan written back as core's normalized JSON, so the corpus walker proves the typed
//! mapper lost nothing; `canon` folds the forms the types merge on purpose.

use serde_json::{json, Map, Number, Value};
use stencil_mcp::opplan::{Action, Axis, Dir, FormulaOp, PageSize};

/// Every number as its double, so `2` and `2.0` compare equal as they do in JS.
pub fn numbers(v: Value) -> Value {
    match v {
        Value::Number(n) => n.as_f64().and_then(Number::from_f64).map_or(Value::Null, Value::Number),
        Value::Array(list) => Value::Array(list.into_iter().map(numbers).collect()),
        Value::Object(map) => Value::Object(map.into_iter().map(|(k, v)| (k, numbers(v))).collect()),
        other => other,
    }
}

fn axis(a: &Axis) -> &'static str {
    if *a == Axis::X { "x" } else { "y" }
}

fn some(o: &mut Map<String, Value>, key: &str, value: &Option<String>) {
    if let Some(v) = value {
        o.insert(key.into(), v.clone().into());
    }
}

pub fn action_json(a: &Action) -> Value {
    let mut o = Map::new();
    o.insert("op".into(), a.op_name().into());
    match a {
        Action::Crop { x1, x2, y1, y2, aspect } => {
            let mut spec = Map::new();
            for (k, v) in [("x1", x1), ("x2", x2), ("y1", y1), ("y2", y2), ("aspect", aspect)] {
                some(&mut spec, k, v);
            }
            o.insert("spec".into(), Value::Object(spec));
        }
        Action::Rotate { dir, times } => {
            o.insert("dir".into(), if *dir == Dir::Left { "left" } else { "right" }.into());
            o.insert("times".into(), (*times).into());
        }
        Action::Filter { mode, tint } => {
            o.insert("mode".into(), format!("{mode:?}").to_lowercase().into());
            some(&mut o, "tint", tint);
        }
        Action::Layout { lines } => {
            o.insert("lines".into(), serde_json::to_value(lines).expect("lines serialize"));
        }
        Action::Formula(FormulaOp::Set { axis: x, expr }) => {
            o.insert("axis".into(), axis(x).into());
            o.insert("expr".into(), expr.clone().into());
        }
        Action::Formula(FormulaOp::Clear { axis: x }) => {
            o.insert("axis".into(), axis(x).into());
            o.insert("expr".into(), "".into());
        }
        Action::Formula(FormulaOp::Enable(on)) => {
            o.insert("enabled".into(), (*on).into());
        }
        Action::Page { size: PageSize::Format(format) } => {
            o.insert("format".into(), format.clone().into());
        }
        Action::Page { size: PageSize::Cm { width, height } } | Action::Blank { dims_cm: Some((width, height)), .. } => {
            o.insert("width".into(), (*width).into());
            o.insert("height".into(), (*height).into());
        }
        Action::Blank { .. } => {}
        Action::Frame { indices } => {
            o.insert("indices".into(), json!(indices));
        }
        Action::Image { index } => {
            o.insert("index".into(), (*index).into());
        }
        Action::Save { name, path } => {
            some(&mut o, "name", name);
            some(&mut o, "path", path);
        }
    }
    if let Action::Blank { color, format, .. } = a {
        o.insert("color".into(), color.clone().into());
        some(&mut o, "format", format);
    }
    numbers(Value::Object(o))
}

/// A golden action with the typed forms folded: one frame index is a list of one, a blank
/// formula expression clears, an empty save path is none.
pub fn canon(action: &Value) -> Value {
    let mut o = action.as_object().cloned().unwrap_or_default();
    if o["op"] == "frame" {
        if let Some(index) = o.remove("index") {
            o.insert("indices".into(), json!([index]));
        }
    }
    if o["op"] == "formula" && o.get("expr").and_then(Value::as_str).is_some_and(|e| e.trim().is_empty()) {
        o.insert("expr".into(), "".into());
    }
    if o["op"] == "save" && o.get("path").and_then(Value::as_str) == Some("") {
        o.remove("path");
    }
    numbers(Value::Object(o))
}
