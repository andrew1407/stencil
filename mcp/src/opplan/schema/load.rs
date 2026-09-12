//! Registry → [`Entry`]: the surface's own entries in prompt order, and the §8 op sets.

use serde_json::Value;

use super::json::{object, strings};
use super::{Entry, JsonObject, OpsetEntry, Schema, REGISTRY_JSON};

impl Schema {
    pub(super) fn load(surface: &str) -> Schema {
        let registry: Value = serde_json::from_str(REGISTRY_JSON).expect("opRegistry.json parses");
        let profile = registry["$meta"]["surfaceProfiles"][surface]
            .as_str()
            .unwrap_or_else(|| panic!("opRegistry: unknown surface \"{surface}\""))
            .to_string();
        let order = strings(&registry["profiles"][&profile]["ops"]);
        // `map[surface] ?? map[profile]`: a surface- or profile-specific replacement.
        let for_surface = |map: &Value| -> Option<Value> {
            [surface, profile.as_str()]
                .iter()
                .map(|k| &map[*k])
                .find(|v| !v.is_null())
                .cloned()
        };
        let mut entries: Vec<Entry> = registry["ops"]
            .as_array()
            .map(|ops| ops.iter())
            .into_iter()
            .flatten()
            .filter(|e| strings(&e["profiles"]).iter().any(|p| *p == profile))
            .filter(|e| {
                e["surfaces"].is_null() || strings(&e["surfaces"]).iter().any(|s| s == surface)
            })
            .map(|e| {
                let keys = if e["surfaceKeys"][surface].is_object() {
                    &e["surfaceKeys"][surface]
                } else {
                    &e["keys"]
                };
                let bullet = match for_surface(&e["bulletVariants"]) {
                    Some(Value::String(variant)) => Some(variant),
                    _ => e["bullet"].as_str().map(str::to_string),
                };
                let mut flags = object(&e["flags"]);
                if let Some(Value::Object(extra)) = for_surface(&e["surfaceFlags"]) {
                    flags.extend(extra);
                }
                Entry {
                    name: e["name"].as_str().unwrap_or_default().to_string(),
                    keys: object(keys),
                    rules: strings(&e["rules"]),
                    flags,
                    bullet,
                    holder: e.clone(),
                }
            })
            .collect();
        entries.sort_by_key(|e| {
            order
                .iter()
                .position(|n| *n == e.name)
                .map_or(-1, |i| i as i64)
        });
        let forbidden = strings(&registry["forbidden"]["perSurface"][surface]);
        Schema {
            registry,
            profile,
            entries,
            forbidden,
        }
    }

    /// Resolve an op inside a nested op set (§8 open.actions).
    pub fn opset_entry(&self, set: &str, op: &str) -> OpsetEntry {
        let os = &self.registry["opsets"][set];
        assert!(!os.is_null(), "opRegistry: unknown opset \"{set}\"");
        if os["overrides"][op].is_object() {
            let over = &os["overrides"][op];
            return OpsetEntry::Entry(Entry {
                name: op.into(),
                keys: object(&over["keys"]),
                rules: strings(&over["rules"]),
                flags: JsonObject::new(),
                bullet: None,
                holder: over.clone(),
            });
        }
        if strings(&os["ops"]).iter().any(|o| o == op) {
            let found = self.registry["ops"]
                .as_array()
                .into_iter()
                .flatten()
                .find(|e| e["id"] == op);
            return match found {
                Some(e) => OpsetEntry::Entry(Entry {
                    name: e["name"].as_str().unwrap_or_default().into(),
                    keys: object(&e["keys"]),
                    rules: strings(&e["rules"]),
                    flags: object(&e["flags"]),
                    bullet: e["bullet"].as_str().map(str::to_string),
                    holder: e.clone(),
                }),
                None => OpsetEntry::Unknown,
            };
        }
        if strings(&os["failOps"]).iter().any(|o| o == op) {
            return OpsetEntry::Fail;
        }
        OpsetEntry::Unknown
    }
}
