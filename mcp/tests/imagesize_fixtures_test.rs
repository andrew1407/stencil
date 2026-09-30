//! The shared image-header corpus (`common/fixtures/imageHeader/cases.json`)
//! walked through `sniff_info`, one reported case per header. Disagreements live in
//! `tests/fixture_overrides.json` (family `imageHeader`).

use std::sync::LazyLock;

use base64::Engine;
use serde_json::Value;
use stencil_mcp::imagesize::sniff_info;

mod common;
use common::walk::Walk;

static CASES: LazyLock<Vec<Value>> = LazyLock::new(|| {
    serde_json::from_str(include_str!("../../common/fixtures/imageHeader/cases.json"))
        .expect("the image-header corpus is a JSON array")
});

fn check(case: &Value) {
    let name = case["name"].as_str().expect("case name");
    let pinned = &common::overrides("imageHeader")[name];
    let expect = if pinned.is_null() { &case["expect"] } else { &pinned["expect"] };
    let bytes = base64::engine::general_purpose::STANDARD
        .decode(case["base64"].as_str().expect("base64"))
        .expect("the header decodes");
    let measured = sniff_info(&bytes).map(|i| {
        serde_json::json!({ "format": i.format, "width": i.width, "height": i.height })
    });
    assert_eq!(measured.as_ref().unwrap_or(&Value::Null), expect, "{name}");
}

fn main() {
    let mut walk = Walk::new();
    for case in &*CASES {
        walk.case(format!("imageHeader/{}", case["name"].as_str().unwrap()), || check(case));
    }
    walk.run()
}
