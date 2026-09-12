//! One object against a key map: unknown keys, the holder's presence rules, each
//! declared key's dependencies and value — then the normalized pick of what survived.

use serde_json::Value;

use super::json::{json_eq, object, present, quote};
use super::path::Path;
use super::rules::check_presence;
use super::{Check, JsonObject, Schema};

impl Schema {
    /// One object against a key map + its holder's presence rules. `skip` names keys
    /// that are neither declared nor unknown (the action's own "op").
    pub(super) fn check_fields(
        &self,
        obj: &JsonObject,
        fields: &JsonObject,
        holder: &Value,
        path: Option<&Path>,
        skip: &[&str],
    ) -> Check {
        let at = |key: &str| Path::child(path, key);
        // `allowUnknown` (the envelope's variant objects) tolerates undeclared keys.
        if holder["allowUnknown"] != Value::Bool(true) {
            for k in obj.keys() {
                if !skip.contains(&k.as_str()) && !fields.contains_key(k) {
                    let where_ = path
                        .map(|p| format!(" in {}", p.where_()))
                        .unwrap_or_default();
                    return Err(format!("unknown field \"{k}\"{where_}"));
                }
            }
        }
        check_presence(obj, fields, holder)?;
        for (k, spec) in fields {
            if !present(obj, k) {
                if spec["required"] == Value::Bool(true) {
                    return Err(format!("{} is required", at(k).label()));
                }
                if let Some(deps) = spec["requiredWith"].as_object() {
                    for (dep, vals) in deps {
                        let have = obj.get(dep).cloned().unwrap_or(Value::Null);
                        if vals
                            .as_array()
                            .is_some_and(|vals| vals.iter().any(|v| json_eq(v, &have)))
                        {
                            return Err(format!(
                                "{} is required with \"{dep}\" {}",
                                at(k).label(),
                                quote(&have)
                            ));
                        }
                    }
                }
                continue;
            }
            if let Some(deps) = spec["onlyWith"].as_object() {
                for (dep, vals) in deps {
                    let have = obj.get(dep).cloned().unwrap_or(Value::Null);
                    let vals = vals.as_array().cloned().unwrap_or_default();
                    if !vals.iter().any(|v| json_eq(v, &have)) {
                        let wanted = vals.iter().map(quote).collect::<Vec<_>>().join(" or ");
                        return Err(format!(
                            "{} only applies with \"{dep}\" {wanted}",
                            at(k).label()
                        ));
                    }
                }
            }
            self.check_value(&obj[k], spec, &at(k), Some(obj))?;
        }
        Ok(())
    }

    // ── normalization: the declared keys only, defaults applied, trims honoured ──

    fn pick(&self, v: &Value, spec: &Value) -> Value {
        match (spec["type"].as_str(), v) {
            (Some("object"), Value::Object(obj)) if !spec["fields"].is_null() => {
                Value::Object(self.pick_fields(obj, &object(&spec["fields"])))
            }
            (Some("array"), Value::Array(list)) if !spec["items"].is_null() => {
                Value::Array(list.iter().map(|x| self.pick(x, &spec["items"])).collect())
            }
            (Some("string"), Value::String(s)) if spec["trim"] == Value::Bool(true) => {
                Value::String(s.trim().into())
            }
            _ => v.clone(),
        }
    }

    pub(super) fn pick_fields(&self, obj: &JsonObject, fields: &JsonObject) -> JsonObject {
        let mut out = JsonObject::new();
        for (k, spec) in fields {
            if present(obj, k) {
                out.insert(k.clone(), self.pick(&obj[k], spec));
            } else if !spec["default"].is_null() {
                out.insert(k.clone(), spec["default"].clone());
            }
        }
        out
    }
}
