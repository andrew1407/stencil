//! The embedded `opRegistry.json`, read for this surface: its mcp-profile entries in prompt
//! order, the forbidden names, the limits and the fingerprint the CLI's `--plan-check`
//! reports for its own copy (`cli/CONTRACT.md` §7). Validation lives in core, not here.

use std::sync::OnceLock;

use serde_json::Value;

pub const REGISTRY_JSON: &str = include_str!("../../../browser/js/config/llm/opRegistry.json");

/// This binary's surface; `$meta.surfaceProfiles` maps it to its profile.
pub const SURFACE: &str = "mcp";

/// One op this surface registers: its name, its §4 bullet and its §2.1 flag.
#[derive(Debug, Clone)]
pub struct Entry {
    pub name: String,
    pub bullet: String,
    pub top_level_only: bool,
}

fn registry() -> &'static Value {
    static REGISTRY: OnceLock<Value> = OnceLock::new();
    REGISTRY.get_or_init(|| serde_json::from_str(REGISTRY_JSON).expect("opRegistry.json parses"))
}

fn strings(v: &Value) -> Vec<String> {
    v.as_array().into_iter().flatten().filter_map(Value::as_str).map(str::to_string).collect()
}

/// The surface's entries in profile (= prompt) order, bullet variants and flags resolved.
pub fn entries() -> &'static [Entry] {
    static ENTRIES: OnceLock<Vec<Entry>> = OnceLock::new();
    ENTRIES.get_or_init(|| {
        let r = registry();
        let profile = r["$meta"]["surfaceProfiles"][SURFACE].as_str().expect("the mcp profile");
        let order = strings(&r["profiles"][profile]["ops"]);
        // `map[surface] ?? map[profile]`: a surface- or profile-specific replacement.
        let pick =
            |map: &Value| [SURFACE, profile].iter().map(|k| map[*k].clone()).find(|v| !v.is_null());
        let mut entries: Vec<Entry> = r["ops"]
            .as_array()
            .into_iter()
            .flatten()
            .filter(|e| strings(&e["profiles"]).iter().any(|p| p == profile))
            .filter(|e| {
                e["surfaces"].is_null() || strings(&e["surfaces"]).iter().any(|s| s == SURFACE)
            })
            .map(|e| {
                let bullet = match pick(&e["bulletVariants"]) {
                    Some(Value::String(variant)) => variant,
                    _ => e["bullet"].as_str().unwrap_or_default().to_string(),
                };
                let flag = pick(&e["surfaceFlags"]).and_then(|f| f["topLevelOnly"].as_bool());
                Entry {
                    name: e["name"].as_str().unwrap_or_default().to_string(),
                    bullet,
                    top_level_only: flag.unwrap_or(e["flags"]["topLevelOnly"] == true),
                }
            })
            .collect();
        entries.sort_by_key(|e| order.iter().position(|n| *n == e.name).map_or(-1, |i| i as i64));
        entries
    })
}

/// §13's never-model-drivable names for this surface (`forbidden.perSurface.mcp`).
pub fn forbidden() -> &'static [String] {
    static FORBIDDEN: OnceLock<Vec<String>> = OnceLock::new();
    FORBIDDEN.get_or_init(|| strings(&registry()["forbidden"]["perSurface"][SURFACE]))
}

/// A limit by its dotted name (`MAX_STRING_CHARS`, `ask.maxOptions`).
pub fn limit(name: &str) -> Option<f64> {
    name.split('.').try_fold(&registry()["limits"], |cur, k| cur.get(k))?.as_f64()
}

/// The embedded registry's size and FNV-1a 64 (16 lowercase hex digits), as the CLI reports.
pub fn fingerprint() -> (u64, String) {
    let hash = REGISTRY_JSON.bytes().fold(0xcbf2_9ce4_8422_2325_u64, |h, b| {
        (h ^ u64::from(b)).wrapping_mul(0x0000_0100_0000_01b3)
    });
    (REGISTRY_JSON.len() as u64, format!("{hash:016x}"))
}
