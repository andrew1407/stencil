//! Registry-driven op-plan schema engine — a rule-for-rule port of the reference
//! `browser/js/llm/opSchema.js` for this surface (opRegistry.README.md documents the
//! key-spec language and the check order). The registry is embedded at compile time and
//! parsed once; there is no regex crate, so each named grammar in `regexes` is a
//! hand-written matcher (`matches`).

use std::sync::OnceLock;

use serde_json::{Map, Value};

pub type JsonObject = Map<String, Value>;

/// This binary's surface; `$meta.surfaceProfiles` maps it to its profile.
pub const SURFACE: &str = "mcp";

const REGISTRY_JSON: &str = include_str!("../../../browser/js/config/llm/opRegistry.json");

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

// ── JSON helpers ──

fn strings(v: &Value) -> Vec<String> {
    v.as_array()
        .map(|a| {
            a.iter()
                .filter_map(Value::as_str)
                .map(str::to_string)
                .collect()
        })
        .unwrap_or_default()
}

fn object(v: &Value) -> JsonObject {
    v.as_object().cloned().unwrap_or_default()
}

fn present(obj: &JsonObject, key: &str) -> bool {
    obj.get(key).is_some_and(|v| !v.is_null())
}

fn as_finite(v: &Value) -> Option<f64> {
    v.as_f64().filter(|f| f.is_finite())
}

fn is_int(v: &Value) -> bool {
    as_finite(v).is_some_and(|f| f.fract() == 0.0)
}

/// JS `includes` equality: numbers compare by value (3 == 3.0), the rest structurally.
fn json_eq(a: &Value, b: &Value) -> bool {
    match (as_finite(a), as_finite(b)) {
        (Some(x), Some(y)) => x == y,
        _ => a == b,
    }
}

fn quote(v: &Value) -> String {
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

fn quote_list(vals: &[Value]) -> String {
    vals.iter().map(quote).collect::<Vec<_>>().join(", ")
}

// ── Where a value sits, for messages: `"x1" in spec`, `"label" in ask.options[2]` ──

#[derive(Clone, Default)]
struct Path {
    root: String,
    key: String,
    container: Option<String>,
}

impl Path {
    fn at(root: &str, key: &str) -> Path {
        Path {
            root: root.into(),
            key: key.into(),
            container: None,
        }
    }
    fn where_(&self) -> String {
        if self.key.is_empty() {
            return self.root.trim_end_matches('.').to_string();
        }
        let prefix = self
            .container
            .as_ref()
            .map(|c| format!("{c}."))
            .unwrap_or_default();
        format!("{prefix}{}{}", self.root, self.key)
    }
    fn label(&self) -> String {
        let suffix = self
            .container
            .as_ref()
            .map(|c| format!(" in {c}"))
            .unwrap_or_default();
        format!("\"{}{}\"{suffix}", self.root, self.key)
    }
    fn child(parent: Option<&Path>, key: &str) -> Path {
        match parent {
            Some(p) if !p.key.is_empty() => Path {
                root: String::new(),
                key: key.into(),
                container: Some(p.where_()),
            },
            Some(p) => Path::at(&p.root, key),
            None => Path::at("", key),
        }
    }
    fn item(&self, i: usize) -> Path {
        Path {
            root: self.root.clone(),
            key: format!("{}[{i}]", self.key),
            container: self.container.clone(),
        }
    }
}

// ── Hand-written matchers for `regexes` (flag-free sources, one per grammar) ──

/// `^-?(\d+(\.\d+)?|\.\d+)(%|px|cm|in)?$`
fn is_crop_token(token: &str) -> bool {
    let rest = token.strip_prefix('-').unwrap_or(token);
    let unit_start = rest
        .find(|c: char| !c.is_ascii_digit() && c != '.')
        .unwrap_or(rest.len());
    let (number, unit) = rest.split_at(unit_start);
    let digits = |s: &str| s.bytes().all(|b| b.is_ascii_digit());
    let valid_number = match number.split_once('.') {
        None => !number.is_empty() && digits(number),
        Some((int, frac)) => !frac.is_empty() && digits(frac) && digits(int),
    };
    valid_number && matches!(unit, "" | "%" | "px" | "cm" | "in")
}

/// `^0*[1-9]\d*:0*[1-9]\d*$` — both sides > 0.
fn is_crop_aspect(token: &str) -> bool {
    let positive = |s: &str| {
        !s.is_empty() && s.bytes().all(|b| b.is_ascii_digit()) && s.bytes().any(|b| b != b'0')
    };
    token
        .split_once(':')
        .is_some_and(|(w, h)| positive(w) && positive(h))
}

/// `^[abc](10|[0-9])$`
fn is_page_format(format: &str) -> bool {
    let mut chars = format.chars();
    let Some(series) = chars.next() else {
        return false;
    };
    let number = chars.as_str();
    matches!(series, 'a' | 'b' | 'c')
        && (number == "10" || (number.len() == 1 && number.bytes().all(|b| b.is_ascii_digit())))
}

/// `^#[0-9a-fA-F]{6}$`
fn is_hex(color: &str) -> bool {
    color
        .strip_prefix('#')
        .is_some_and(|hex| hex.len() == 6 && hex.bytes().all(|b| b.is_ascii_hexdigit()))
}

/// `^[0-9<var>+\-*/(). ]+$`
fn is_formula(expr: &str, var: char) -> bool {
    !expr.is_empty()
        && expr
            .chars()
            .all(|c| c.is_ascii_digit() || "+-*/(). ".contains(c) || c == var)
}

/// JS `\s` (no `u` flag): Unicode White_Space plus U+FEFF, minus U+0085.
fn is_js_space(c: char) -> bool {
    c == '\u{feff}' || (c.is_whitespace() && c != '\u{85}')
}

/// `^[hH][tT][tT][pP][sS]?://\S+$`
fn is_http_url(s: &str) -> bool {
    let lower: String = s.chars().take(8).collect::<String>().to_ascii_lowercase();
    let rest = if lower.starts_with("https://") {
        &s[8..]
    } else if lower.starts_with("http://") {
        &s[7..]
    } else {
        return false;
    };
    !rest.is_empty() && !rest.chars().any(is_js_space)
}

/// `^[a-zA-Z][a-zA-Z0-9+.\-]*://` — a prefix match.
fn has_url_scheme(s: &str) -> bool {
    let Some(pos) = s.find("://") else {
        return false;
    };
    let mut chars = s[..pos].chars();
    matches!(chars.next(), Some(c) if c.is_ascii_alphabetic())
        && chars.all(|c| c.is_ascii_alphanumeric() || matches!(c, '+' | '.' | '-'))
}

/// Match `s` against the registry grammar `name`.
pub fn matches(name: &str, s: &str) -> bool {
    match name {
        "CROP_TOKEN" => is_crop_token(s),
        "CROP_ASPECT" => is_crop_aspect(s),
        "PAGE_FORMAT" => is_page_format(s),
        "HEX" => is_hex(s),
        "CSS_NAME" => !s.is_empty() && s.bytes().all(|b| b.is_ascii_alphabetic()),
        "FORMULA_X" => is_formula(s, 'x'),
        "FORMULA_Y" => is_formula(s, 'y'),
        "HTTP_URL" => is_http_url(s),
        "URL_SCHEME" => has_url_scheme(s),
        other => panic!("opRegistry: no matcher for regex \"{other}\""),
    }
}

// ── native cross-field rules an entry may name in `rules` ──

/// §3.2 tolerance: "aspect" beside "spec" folds into the spec when it lacks one; a
/// conflicting duplicate fails. The folded copy is what gets validated + normalized.
fn crop_aspect_fold(mut a: JsonObject) -> Result<JsonObject, String> {
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

impl Schema {
    fn load(surface: &str) -> Schema {
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

    // ── value checks ──

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

    fn check_value(
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

    /// One object against a key map + its holder's presence rules. `skip` names keys
    /// that are neither declared nor unknown (the action's own "op").
    fn check_fields(
        &self,
        obj: &JsonObject,
        fields: &JsonObject,
        holder: &Value,
        path: Option<&Path>,
        skip: &[&str],
    ) -> Check {
        let at = |key: &str| Path::child(path, key);
        let quoted = |group: &Value| {
            strings(group)
                .iter()
                .map(|k| format!("\"{k}\""))
                .collect::<Vec<_>>()
        };
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

    fn pick_fields(&self, obj: &JsonObject, fields: &JsonObject) -> JsonObject {
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

    // ── public surface ──

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
