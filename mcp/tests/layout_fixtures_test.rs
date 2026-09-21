//! What mcp ACCEPTS as layout JSON: hand-written documents, then the shared corpus
//! (`browser/js/config/fixtures/layout/`) walked through the real types.
//!
//! mcp's analog of `sanitizeLines` is the serde round-trip the `--layout` temp file goes
//! through. Disagreements live in `tests/fixture_overrides.json` (family `layout`).

use std::sync::LazyLock;

use serde_json::{json, Value};
use stencil_mcp::layout::{Layout, Line};

mod common;
use common::walk::Walk;

const FIXTURES_DIR: &str =
    concat!(env!("CARGO_MANIFEST_DIR"), "/../browser/js/config/fixtures/layout");

/// `layoutFields.json`, the canonical table every surface's export order comes from.
const FIELDS_PATH: &str =
    concat!(env!("CARGO_MANIFEST_DIR"), "/../browser/js/config/layoutFields.json");

/// Top-level corpus keys mcp's `Layout` can represent at all, in struct order; serde silently
/// drops the rest. `imageFilter` is the canonical wire key (legacy `filter` is read-only).
const MCP_VISIBLE_KEYS: [&str; 7] = [
    "imageWidth",
    "imageHeight",
    "lines",
    "imageFilter",
    "pageSize",
    "customPageWidth",
    "customPageHeight",
];

static PAYLOAD: LazyLock<Vec<Value>> = LazyLock::new(|| load("payload.json"));
static SPARSE: LazyLock<Vec<Value>> = LazyLock::new(|| load("sparse.json"));

fn load(file: &str) -> Vec<Value> {
    let path = format!("{FIXTURES_DIR}/{file}");
    let raw = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cannot read {path}: {e}"));
    serde_json::from_str::<Vec<Value>>(&raw)
        .unwrap_or_else(|e| panic!("{file} is not a JSON array: {e}"))
}

/// `payload.json` replayed as a `Layout` round-trip. Override `verdict: "reject"` pins a
/// vector serde refuses; `payload` pins a round-trip differing by the invisible keys.
fn check_payload(vector: &Value) {
    let name = vector["name"].as_str().expect("vector name");
    let ov = &common::overrides("layout")[name];
    let parsed: Result<Layout, _> = serde_json::from_value(vector["layout"].clone());
    if ov["verdict"].as_str() == Some("reject") {
        assert!(parsed.is_err(), "[{name}] pinned as serde-rejected, but it parsed");
        return;
    }
    let layout = parsed.unwrap_or_else(|e| panic!("[{name}] mcp Layout rejected: {e}"));
    let got = serde_json::to_value(&layout).unwrap();
    let want = if ov["payload"].is_null() { &vector["expectPayload"] } else { &ov["payload"] };
    assert!(
        structurally_equal(&got, want),
        "[{name}] round-trip payload\n got: {got}\nwant: {want}"
    );
    // `Value` round-trips sort keys, so top-level key order is checked on the raw
    // serialization instead.
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

/// `sparse.json`: tolerant-parser vectors. mcp's serde `Vec<Line>` is strict, so vectors
/// relying on coercion are pinned as rejected; the rest fill to `expectFilled`.
fn check_sparse(vector: &Value) {
    let name = vector["name"].as_str().expect("vector name");
    let ov = &common::overrides("layout")[name];
    let parsed: Result<Vec<Line>, _> = serde_json::from_value(vector["sparse"].clone());
    if ov["verdict"].as_str() == Some("reject") {
        assert!(parsed.is_err(), "[{name}] pinned as serde-rejected, but it parsed");
        return;
    }
    let lines = parsed.unwrap_or_else(|e| panic!("[{name}] mcp Vec<Line> rejected: {e}"));
    let got = Value::Array(lines.iter().map(filled).collect());
    assert!(
        structurally_equal(&got, &vector["expectFilled"]),
        "[{name}] filled lines\n got: {got}\nwant: {}",
        vector["expectFilled"]
    );
}

// ── Hand-written documents ──

/// The exact document the CLI's own parser test feeds `cli/src/media/layout.zig` (legacy `filter`
/// key included). If the two ends ever disagree about a key, this stops parsing.
fn parses_the_document_the_cli_parser_test_uses() {
    let doc = r#"{ "imageWidth": 10, "imageHeight": 20, "filter": "bw",
        "lines": [ { "points": [{"x":1,"y":2},{"x":3,"y":4}],
                     "color": "red", "thickness": 3, "locked": true } ] }"#;
    let layout: Layout = serde_json::from_str(doc).expect("the CLI's fixture parses here too");

    assert_eq!(layout.image_width, Some(10.0));
    assert_eq!(layout.image_height, Some(20.0));
    assert_eq!(layout.filter.as_deref(), Some("bw"));
    assert_eq!(layout.lines.len(), 1);

    let line = &layout.lines[0];
    assert_eq!(line.points.len(), 2);
    assert_eq!(line.color.as_deref(), Some("red"));
    assert_eq!(line.thickness, Some(3.0));
    assert_eq!(line.locked, Some(true));
    // Absent in the document → None here, so the CLI supplies its documented defaults
    // (pointSize 4, style solid, fillColor transparent).
    assert_eq!(line.point_size, None);
    assert_eq!(line.style, None);
    assert_eq!(line.fill_color, None);
}

/// Canonical `imageFilter` and legacy `filter` both deserialize into the same field;
/// spelling BOTH is a serde duplicate-field error (no both-present precedence here).
fn filter_reads_canonical_and_legacy_keys() {
    let canonical: Layout = serde_json::from_str(r#"{"imageFilter":"bw"}"#).expect("parses");
    assert_eq!(canonical.filter.as_deref(), Some("bw"));
    let legacy: Layout = serde_json::from_str(r#"{"filter":"sepia"}"#).expect("parses");
    assert_eq!(legacy.filter.as_deref(), Some("sepia"));
    let both: Result<Layout, _> = serde_json::from_str(r#"{"filter":"a","imageFilter":"b"}"#);
    assert!(both.is_err(), "both spellings at once is a duplicate-field error");
}

/// The struct's field order is a subsequence of the canonical export order, so every document
/// mcp writes carries the browser's key order with the unrepresented fields left out.
fn visible_keys_follow_the_canonical_export_order() {
    let raw = std::fs::read_to_string(FIELDS_PATH).expect("cannot read layoutFields.json");
    let fields: Vec<Value> = serde_json::from_str(&raw).expect("layoutFields.json is an array");
    let mut canon: Vec<(u64, &str)> = fields
        .iter()
        .filter_map(|f| Some((f["export"].as_u64()?, f["key"].as_str()?)))
        .collect();
    canon.sort_unstable();
    let ordered: Vec<&str> =
        canon.iter().map(|&(_, k)| k).filter(|k| MCP_VISIBLE_KEYS.contains(k)).collect();
    assert_eq!(ordered, MCP_VISIBLE_KEYS, "Layout's field order drifted from layoutFields.json");
}

/// A layout with no `lines` key is legal (`#[serde(default)]`) and means "draw nothing" —
/// a filter-only layout is a real use of the flag.
fn missing_lines_defaults_to_empty() {
    let layout: Layout = serde_json::from_str(r#"{"filter":"sepia"}"#).expect("parses");
    assert!(layout.lines.is_empty());
    assert_eq!(layout.filter.as_deref(), Some("sepia"));
}

/// A line with no points is legal on the wire; the CLI skips it rather than erroring.
fn a_line_with_no_points_round_trips() {
    let layout: Layout = serde_json::from_str(r#"{"lines":[{"points":[]}]}"#).expect("parses");
    assert_eq!(layout.lines.len(), 1);
    assert!(layout.lines[0].points.is_empty());
}

/// Unknown keys from a newer browser export must be ignored, never rejected — an older
/// server should still draw the lines it understands.
fn unknown_fields_are_ignored() {
    let doc = r#"{"imageWidth":10,"futureKey":{"a":1},
                  "lines":[{"points":[{"x":0,"y":0}],"futureLineKey":true}]}"#;
    let layout: Layout = serde_json::from_str(doc).expect("unknown keys must not be fatal");
    assert_eq!(layout.image_width, Some(10.0));
    assert_eq!(layout.lines.len(), 1);
}

fn main() {
    let mut walk = Walk::new();
    for vector in &*PAYLOAD {
        walk.case(format!("payload/{}", vector["name"].as_str().unwrap()), || check_payload(vector));
    }
    for vector in &*SPARSE {
        walk.case(format!("sparse/{}", vector["name"].as_str().unwrap()), || check_sparse(vector));
    }
    walk.case("parses_the_document_the_cli_parser_test_uses", parses_the_document_the_cli_parser_test_uses);
    walk.case("filter_reads_canonical_and_legacy_keys", filter_reads_canonical_and_legacy_keys);
    walk.case("visible_keys_follow_the_canonical_export_order", visible_keys_follow_the_canonical_export_order);
    walk.case("missing_lines_defaults_to_empty", missing_lines_defaults_to_empty);
    walk.case("a_line_with_no_points_round_trips", a_line_with_no_points_round_trips);
    walk.case("unknown_fields_are_ignored", unknown_fields_are_ignored);
    walk.run()
}
