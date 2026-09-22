//! The registry's cross-field rules: the native ones an entry names in `rules`, and the
//! presence rules (`forms` / `together` / `exclusive` / `minFields`) a holder declares.

use serde_json::Value;

use super::json::{as_finite, json_eq, object, present, strings};
use super::{Check, JsonObject};

/// §3.2 tolerance: "aspect" beside "spec" folds into the spec when it lacks one; a
/// conflicting duplicate fails. The folded copy is what gets validated + normalized.
pub(super) fn crop_aspect_fold(mut a: JsonObject) -> Result<JsonObject, String> {
    if !present(&a, "aspect") || !a.get("spec").is_some_and(Value::is_object) {
        return Ok(a);
    }
    let aspect = a.remove("aspect").unwrap_or(Value::Null);
    let mut spec = object(&a["spec"]);
    if present(&spec, "aspect") && !json_eq(&spec["aspect"], &aspect) {
        return Err(
            "\"aspect\" appears both beside \"spec\" and inside it with different values".into(),
        );
    }
    spec.entry("aspect").or_insert(aspect);
    a.insert("spec".into(), Value::Object(spec));
    Ok(a)
}

/// A holder's presence rules, in check order. `fields` is the declared key map.
pub(super) fn check_presence(obj: &JsonObject, fields: &JsonObject, holder: &Value) -> Check {
    let quoted = |group: &Value| {
        strings(group)
            .iter()
            .map(|k| format!("\"{k}\""))
            .collect::<Vec<_>>()
    };
    if let Some(forms) = holder["forms"].as_array() {
        let in_forms: Vec<String> = forms.iter().flat_map(strings).collect();
        let given: Vec<&String> = fields
            .keys()
            .filter(|k| in_forms.contains(*k) && present(obj, k))
            .collect();
        let matched = forms
            .iter()
            .map(strings)
            .filter(|f| f.len() == given.len() && f.iter().all(|k| given.contains(&k)))
            .count();
        if matched != 1 {
            let options = forms
                .iter()
                .map(|f| quoted(f).join("+"))
                .collect::<Vec<_>>()
                .join(" / ");
            return Err(format!("exactly one of {options} is required"));
        }
    }
    for group in holder["together"].as_array().into_iter().flatten() {
        let keys = strings(group);
        let n = keys.iter().filter(|k| present(obj, k)).count();
        if n != 0 && n != keys.len() {
            return Err(format!("{} ride together", quoted(group).join(" and ")));
        }
    }
    for group in holder["exclusive"].as_array().into_iter().flatten() {
        if strings(group).iter().filter(|k| present(obj, k)).count() > 1 {
            return Err(format!(
                "carries both {} — at most one of them",
                quoted(group).join(" and ")
            ));
        }
    }
    if let Some(min) = as_finite(&holder["minFields"]) {
        let n = fields.keys().filter(|k| present(obj, k)).count() as f64;
        if n < min {
            let how_many = if min == 1.0 {
                "one".to_string()
            } else {
                min.to_string()
            };
            let names = fields.keys().cloned().collect::<Vec<_>>().join("/");
            return Err(format!("needs at least {how_many} of {names}"));
        }
    }
    Ok(())
}
