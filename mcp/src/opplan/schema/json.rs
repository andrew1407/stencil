//! Small JSON readers shared by the checks — JS semantics, not Rust's.

use serde_json::Value;

use super::JsonObject;

pub(super) fn strings(v: &Value) -> Vec<String> {
    v.as_array()
        .map(|a| {
            a.iter()
                .filter_map(Value::as_str)
                .map(str::to_string)
                .collect()
        })
        .unwrap_or_default()
}

pub(super) fn object(v: &Value) -> JsonObject {
    v.as_object().cloned().unwrap_or_default()
}

pub(super) fn present(obj: &JsonObject, key: &str) -> bool {
    obj.get(key).is_some_and(|v| !v.is_null())
}

pub(super) fn as_finite(v: &Value) -> Option<f64> {
    v.as_f64().filter(|f| f.is_finite())
}

pub(super) fn is_int(v: &Value) -> bool {
    as_finite(v).is_some_and(|f| f.fract() == 0.0)
}

/// JS `includes` equality: numbers compare by value (3 == 3.0), the rest structurally.
pub(super) fn json_eq(a: &Value, b: &Value) -> bool {
    match (as_finite(a), as_finite(b)) {
        (Some(x), Some(y)) => x == y,
        _ => a == b,
    }
}

pub(super) fn quote(v: &Value) -> String {
    match v {
        Value::String(s) => format!("\"{s}\""),
        Value::Number(n) => n.as_f64().map_or_else(
            || n.to_string(),
            |f| {
                if f.fract() == 0.0 {
                    format!("{}", f as i64)
                } else {
                    f.to_string()
                }
            },
        ),
        other => other.to_string(),
    }
}

pub(super) fn quote_list(vals: &[Value]) -> String {
    vals.iter().map(quote).collect::<Vec<_>>().join(", ")
}
