//! Registry-driven op-plan schema engine — a rule-for-rule port of the reference
//! `browser/js/llm/opSchema.js` for this surface (opRegistry.README.md documents the
//! key-spec language and the check order). The registry is embedded at compile time and
//! parsed once; there is no regex crate, so each named grammar in `regexes` is a
//! hand-written matcher ([`matches`]).
//!
//! The engine sits in submodules: [`json`] value helpers, [`path`] message locations,
//! [`grammars`] the matchers, [`load`] registry → [`Entry`], [`checks`] one value,
//! [`fields`] one object, [`rules`] the native + presence rules. This file holds the
//! types and the public surface.

use std::sync::OnceLock;

use serde_json::Value;

mod checks;
mod fields;
mod grammars;
mod json;
mod load;
mod path;
mod rules;

pub use grammars::matches;
use json::{as_finite, object};
use rules::crop_aspect_fold;
use path::Path;

pub type JsonObject = serde_json::Map<String, Value>;

/// This binary's surface; `$meta.surfaceProfiles` maps it to its profile.
pub const SURFACE: &str = "mcp";

const REGISTRY_JSON: &str = include_str!("../../../../browser/js/config/llm/opRegistry.json");

type Check = Result<(), String>;

/// One op this surface registers, resolved for it (surface keys, bullet variant, flags).
#[derive(Debug, Clone)]
pub struct Entry {
    pub name: String,
    pub keys: JsonObject,
    pub rules: Vec<String>,
    pub flags: JsonObject,
    pub bullet: Option<String>,
    /// The raw entry — `forms` / `together` / `exclusive` / `minFields` sit on it.
    holder: Value,
}

impl Entry {
    pub fn flag(&self, name: &str) -> bool {
        self.flags.get(name) == Some(&Value::Bool(true))
    }
}

/// An op resolved inside a nested op set (§8 `open.actions`).
pub enum OpsetEntry {
    Entry(Entry),
    Fail,
    Unknown,
}

pub struct Schema {
    registry: Value,
    pub profile: String,
    /// This surface's entries in profile (= prompt) order.
    pub entries: Vec<Entry>,
    pub forbidden: Vec<String>,
}

/// The registry, parsed once for this surface.
pub fn schema() -> &'static Schema {
    static SCHEMA: OnceLock<Schema> = OnceLock::new();
    SCHEMA.get_or_init(|| Schema::load(SURFACE))
}

impl Schema {
    pub fn entry(&self, name: &str) -> Option<&Entry> {
        self.entries.iter().find(|e| e.name == name)
    }

    pub fn is_forbidden(&self, name: &str) -> bool {
        self.forbidden.iter().any(|f| f == name)
    }

    /// A limit by its dotted name (`MAX_ACTIONS`, `ask.label`).
    pub fn limit(&self, name: &str) -> f64 {
        let mut cur = &self.registry["limits"];
        for k in name.split('.') {
            cur = &cur[k];
        }
        cur.as_f64()
            .unwrap_or_else(|| panic!("opRegistry: unknown limit \"{name}\""))
    }

    /// A cap is a number or a limit name.
    fn cap(&self, v: &Value) -> f64 {
        as_finite(v).unwrap_or_else(|| self.limit(v.as_str().unwrap_or_default()))
    }

    fn describe(&self, name: &str) -> String {
        self.registry["regexes"]["describe"][name]
            .as_str()
            .unwrap_or(name)
            .to_string()
    }

    pub fn default_custom_label(&self) -> &str {
        self.registry["ask"]["defaultCustomLabel"]
            .as_str()
            .unwrap_or_default()
    }

    /// Validate one action against its entry (native rules first). Returns the action
    /// as validated (post-fold) — feed it to `normalize`.
    pub fn validate_action(&self, a: &JsonObject, entry: &Entry) -> Result<JsonObject, String> {
        let mut v = a.clone();
        for rule in &entry.rules {
            v = match rule.as_str() {
                "cropAspectFold" => crop_aspect_fold(v)?,
                other => panic!("opRegistry: unknown native rule \"{other}\""),
            };
        }
        self.check_fields(&v, &entry.keys, &entry.holder, None, &["op"])?;
        Ok(v)
    }

    /// `{ op, ...declared keys present (deep-picked), defaults }`.
    pub fn normalize(&self, v: &JsonObject, entry: &Entry) -> JsonObject {
        let mut out = JsonObject::new();
        out.insert("op".into(), Value::String(entry.name.clone()));
        out.extend(self.pick_fields(v, &entry.keys));
        out
    }

    /// The §11 card's structure (option `actions` only shallowly — the caller validates
    /// them as preview actions).
    pub fn validate_ask(&self, ask: &Value) -> Result<(), String> {
        let Value::Object(obj) = ask else {
            return Err("\"ask\" must be an object".into());
        };
        let schema = &self.registry["ask"]["schema"];
        self.check_fields(
            obj,
            &object(&schema["keys"]),
            schema,
            Some(&Path::at("ask.", "")),
            &[],
        )
    }

    pub fn normalize_ask(&self, ask: &JsonObject) -> JsonObject {
        self.pick_fields(ask, &object(&self.registry["ask"]["schema"]["keys"]))
    }

    /// Check one envelope slot ("actions" / "variants") shallowly; `name` is how the
    /// slot is called in messages.
    pub fn check_envelope(&self, v: &Value, key: &str, name: &str) -> Result<(), String> {
        self.check_value(
            v,
            &self.registry["envelope"][key],
            &Path::at("", name),
            None,
        )
    }
}
