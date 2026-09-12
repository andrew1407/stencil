//! One value against one key spec — the `type` switch and its per-type checks.

use serde_json::Value;

use super::json::{as_finite, is_int, json_eq, object, quote_list, strings};
use super::path::Path;
use super::grammars::matches;
use super::{Check, JsonObject, Schema};

impl Schema {
    fn check_string(
        &self,
        v: &Value,
        spec: &Value,
        path: &Path,
        parent: Option<&JsonObject>,
    ) -> Check {
        let Value::String(v) = v else {
            return Err(format!("{} must be a string", path.label()));
        };
        let max = if spec["maxChars"].is_null() {
            self.limit("MAX_STRING_CHARS")
        } else {
            self.cap(&spec["maxChars"])
        };
        if v.chars().count() as f64 > max {
            return Err(format!("{} is longer than {max} characters", path.label()));
        }
        let s = if spec["trim"] == Value::Bool(true) {
            v.trim()
        } else {
            v.as_str()
        };
        if spec["nonEmpty"] == Value::Bool(true) && s.trim().is_empty() {
            return Err(format!("{} must be a non-empty string", path.label()));
        }
        if let Some(allowed) = spec["enum"].as_array() {
            if !allowed.iter().any(|a| a.as_str() == Some(s)) {
                return Err(format!(
                    "{} must be one of {}",
                    path.label(),
                    quote_list(allowed)
                ));
            }
        }
        if spec["literals"]
            .as_array()
            .is_some_and(|l| l.iter().any(|a| a.as_str() == Some(s)))
        {
            return Ok(());
        }
        if spec["blankOk"] == Value::Bool(true) && s.trim().is_empty() {
            return Ok(());
        }
        let mut names = match &spec["regex"] {
            Value::String(one) => vec![one.clone()],
            other => strings(other),
        };
        if let Some(by) = spec["regexBy"].as_object() {
            let dep = by["key"].as_str().unwrap_or_default();
            let value = parent
                .and_then(|p| p.get(dep))
                .and_then(Value::as_str)
                .unwrap_or_default();
            names = by["map"][value]
                .as_str()
                .map(|n| vec![n.to_string()])
                .unwrap_or_default();
        }
        if !names.is_empty() && !names.iter().any(|n| matches(n, s)) {
            let wanted = names
                .iter()
                .map(|n| self.describe(n))
                .collect::<Vec<_>>()
                .join(" or ");
            return Err(format!("{} must be {wanted}", path.label()));
        }
        if let Some(not) = spec["regexNot"].as_str() {
            if matches(not, s) {
                return Err(format!(
                    "{} must be a local value, not {}",
                    path.label(),
                    self.describe(not)
                ));
            }
        }
        Ok(())
    }

    fn check_number(&self, v: &Value, spec: &Value, path: &Path) -> Check {
        let integer = spec["type"] == "integer";
        let noun = if integer { "an integer" } else { "a number" };
        let ok = if integer {
            is_int(v)
        } else {
            as_finite(v).is_some()
        };
        if !ok {
            return Err(format!("{} must be {noun}", path.label()));
        }
        if let Some(allowed) = spec["enum"].as_array() {
            if !allowed.iter().any(|a| json_eq(a, v)) {
                return Err(format!(
                    "{} must be one of {}",
                    path.label(),
                    quote_list(allowed)
                ));
            }
        }
        if let Some(range) = spec["range"].as_array() {
            let n = as_finite(v).unwrap_or_default();
            let lo = range.first().and_then(as_finite);
            let hi = range.get(1).and_then(as_finite);
            if lo.is_some_and(|lo| n < lo) || hi.is_some_and(|hi| n > hi) {
                let bounds = match (lo, hi) {
                    (Some(lo), Some(hi)) => format!("{lo}..{hi}"),
                    (Some(lo), None) => format!(">= {lo}"),
                    (None, hi) => format!("<= {}", hi.unwrap_or_default()),
                };
                return Err(format!("{} must be {noun} {bounds}", path.label()));
            }
        }
        Ok(())
    }

    fn check_boolean(&self, v: &Value, spec: &Value, path: &Path) -> Check {
        if !v.is_boolean() {
            return Err(format!("{} must be a boolean", path.label()));
        }
        if let Some(allowed) = spec["enum"].as_array() {
            if !allowed.contains(v) {
                return Err(format!("{} must be {}", path.label(), quote_list(allowed)));
            }
        }
        Ok(())
    }

    fn check_array(&self, v: &Value, spec: &Value, path: &Path) -> Check {
        let Value::Array(list) = v else {
            return Err(format!("{} must be an array", path.label()));
        };
        let min = (!spec["minItems"].is_null()).then(|| self.cap(&spec["minItems"]));
        let max = (!spec["maxItems"].is_null()).then(|| self.cap(&spec["maxItems"]));
        let len = list.len() as f64;
        if min == Some(1.0) && list.is_empty() {
            return Err(format!("{} must be a non-empty array", path.label()));
        }
        // A real N..M window, not just a cap.
        let window = min.is_some_and(|m| m > 1.0) && max.is_some();
        let window_msg = || {
            format!(
                "{} must hold {}..{} entries",
                path.label(),
                min.unwrap_or_default(),
                max.unwrap_or_default()
            )
        };
        if let Some(max) = max.filter(|max| len > *max) {
            return Err(if window {
                window_msg()
            } else {
                format!("more than {max} entries in {}", path.label())
            });
        }
        if let Some(min) = min.filter(|min| len < *min) {
            return Err(if window {
                window_msg()
            } else {
                format!("{} must hold at least {min} entries", path.label())
            });
        }
        if !spec["items"].is_null() {
            for (i, x) in list.iter().enumerate() {
                self.check_value(x, &spec["items"], &path.item(i), None)?;
            }
        }
        Ok(())
    }

    fn check_object(&self, v: &Value, spec: &Value, path: &Path) -> Check {
        let Value::Object(obj) = v else {
            return Err(format!("{} must be an object", path.label()));
        };
        if !spec["fields"].is_null() || !spec["minFields"].is_null() {
            self.check_fields(obj, &object(&spec["fields"]), spec, Some(path), &[])?;
        }
        Ok(())
    }

    pub(super) fn check_value(
        &self,
        v: &Value,
        spec: &Value,
        path: &Path,
        parent: Option<&JsonObject>,
    ) -> Check {
        match spec["type"].as_str() {
            Some("string") => self.check_string(v, spec, path, parent),
            Some("integer" | "number") => self.check_number(v, spec, path),
            Some("boolean") => self.check_boolean(v, spec, path),
            Some("array") => self.check_array(v, spec, path),
            Some("object") => self.check_object(v, spec, path),
            other => panic!("opRegistry: unknown type {other:?}"),
        }
    }
}
