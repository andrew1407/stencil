//! Walk the shared layout conformance vectors (`browser/js/config/fixtures/layout/`)
//! through mcp's real layout types (`layout::Layout` / `layout::Line`, plain serde).
//! mcp has no `buildLayoutPayload`/`sanitizeLines` — its analog is the serde round-trip
//! the `--layout` temp file goes through — so this PINS what that round-trip does:
//! which corpus fields survive, which are silently invisible, and which inputs the
//! strict (non-tolerant) serde parser rejects. Measured disagreements with the corpus
//! expectations live in `tests/fixture_overrides.json` (family `layout`).

use serde_json::{json, Value};
use stencil_mcp::layout::{Layout, Line};

const FIXTURES_DIR: &str =
    concat!(env!("CARGO_MANIFEST_DIR"), "/../browser/js/config/fixtures/layout");

/// Top-level corpus keys mcp's `Layout` can represent at all. Everything else in a vector's
/// `layout` is silently dropped by serde on round-trip (no deny_unknown_fields).
/// `imageFilter` is the canonical wire key since Phase 6 (legacy `filter` is read-only).
const MCP_VISIBLE_KEYS: [&str; 4] = ["imageWidth", "imageHeight", "imageFilter", "lines"];

fn load(file: &str) -> Vec<Value> {
    let path = format!("{FIXTURES_DIR}/{file}");
    let raw = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cannot read {path}: {e}"));
    serde_json::from_str::<Vec<Value>>(&raw)
        .unwrap_or_else(|e| panic!("{file} is not a JSON array: {e}"))
}

fn overrides() -> Value {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/fixture_overrides.json");
    let raw = std::fs::read_to_string(path).unwrap_or_else(|e| panic!("cannot read {path}: {e}"));
    serde_json::from_str::<Value>(&raw).expect("fixture_overrides.json parses")["layout"].clone()
}

/// `payload.json`: the browser's export-payload vectors, replayed as a `Layout` serde
/// round-trip. An override `verdict: "reject"` pins a vector serde refuses; an override
/// `payload` pins a round-trip that differs from the browser's `expectPayload` (the diff
/// is exactly the keys invisible to mcp, plus the always-emitted `lines`).
#[test]
fn payload_vectors_pin_the_layout_round_trip() {
    let overrides = overrides();
    for vector in load("payload.json") {
        let name = vector["name"].as_str().expect("vector name");
        let ov = &overrides[name];
        let parsed: Result<Layout, _> = serde_json::from_value(vector["layout"].clone());
        if ov["verdict"].as_str() == Some("reject") {
            assert!(parsed.is_err(), "[{name}] pinned as serde-rejected, but it parsed");
            continue;
        }
        let layout = parsed.unwrap_or_else(|e| panic!("[{name}] mcp Layout rejected: {e}"));
        let got = serde_json::to_value(&layout).unwrap();
        let want = if ov["payload"].is_null() { &vector["expectPayload"] } else { &ov["payload"] };
        assert!(
            structurally_equal(&got, want),
            "[{name}] round-trip payload\n got: {got}\nwant: {want}"
        );
        // Everything mcp emits comes from its four visible keys; the real `--layout` temp
        // file serializes the struct directly, so top-level key order is the struct order
        // (Value round-trips sort keys — check order on the raw serialization instead).
        for key in got.as_object().unwrap().keys() {
            assert!(MCP_VISIBLE_KEYS.contains(&key.as_str()), "[{name}] unexpected key {key}");
        }
        let raw = serde_json::to_string(&layout).unwrap();
        let positions: Vec<usize> = MCP_VISIBLE_KEYS
            .iter()
            .filter_map(|k| raw.find(&format!("\"{k}\"")))
            .collect();
        assert!(
            positions.windows(2).all(|w| w[0] < w[1]),
            "[{name}] top-level key order must follow the struct: {raw}"
        );
    }
}

/// The per-line defaults mcp documents (filled by the CLI when a field is omitted;
/// `layout.rs` doc comment + the corpus `_schema.md` cross-surface defaults).
fn filled(line: &Line) -> Value {
    let v = serde_json::to_value(line).unwrap();
    json!({
        "points": v["points"],
        "color": v.get("color").filter(|c| !c.is_null()).cloned().unwrap_or(json!("#FFFF00")),
        "thickness": v.get("thickness").filter(|t| !t.is_null()).cloned().unwrap_or(json!(2)),
        "pointSize": v.get("pointSize").filter(|p| !p.is_null()).cloned().unwrap_or(json!(4)),
        "style": v.get("style").filter(|s| !s.is_null()).cloned().unwrap_or(json!("solid")),
        "locked": v.get("locked").filter(|l| !l.is_null()).cloned().unwrap_or(json!(false)),
        "fillColor": v.get("fillColor").filter(|f| !f.is_null()).cloned().unwrap_or(json!("transparent")),
        "pointColor": v.get("pointColor").filter(|p| !p.is_null()).cloned().unwrap_or(json!("")),
    })
}

/// Structural comparison: key-order-insensitive (per-line key order is not contract) and
/// numeric-identity-insensitive (2 == 2.0 — serde round-trips f64s).
fn structurally_equal(a: &Value, b: &Value) -> bool {
    match (a, b) {
        (Value::Number(x), Value::Number(y)) => x.as_f64() == y.as_f64(),
        (Value::Array(x), Value::Array(y)) => {
            x.len() == y.len() && x.iter().zip(y).all(|(a, b)| structurally_equal(a, b))
        }
        (Value::Object(x), Value::Object(y)) => {
            x.len() == y.len()
                && x.iter().all(|(k, v)| y.get(k).is_some_and(|w| structurally_equal(v, w)))
        }
        _ => a == b,
    }
}

/// `sparse.json`: tolerant-parser vectors. mcp's serde `Vec<Line>` is deliberately strict,
/// so vectors relying on coercion/skip tolerance are pinned as rejected via overrides;
/// vectors that parse must fill to the cross-surface `expectFilled` defaults.
#[test]
fn sparse_vectors_pin_the_strict_line_parser() {
    let overrides = overrides();
    for vector in load("sparse.json") {
        let name = vector["name"].as_str().expect("vector name");
        let ov = &overrides[name];
        let parsed: Result<Vec<Line>, _> = serde_json::from_value(vector["sparse"].clone());
        if ov["verdict"].as_str() == Some("reject") {
            assert!(parsed.is_err(), "[{name}] pinned as serde-rejected, but it parsed");
            continue;
        }
        let lines = parsed.unwrap_or_else(|e| panic!("[{name}] mcp Vec<Line> rejected: {e}"));
        let got = Value::Array(lines.iter().map(filled).collect());
        assert!(
            structurally_equal(&got, &vector["expectFilled"]),
            "[{name}] filled lines\n got: {got}\nwant: {}",
            vector["expectFilled"]
        );
    }
}
