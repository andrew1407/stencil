//! `browser/js/config/llm/fixtures/sanitizer/cases.json` against `sanitize_detail`, one
//! reported case per fixture. Where mcp's word-based sanitizer differs from the browser's,
//! `tests/fixture_overrides.json` pins mcp's own output; every output — pinned or shared —
//! keeps the no-URL and bounded-length invariants.
use std::sync::LazyLock;

use serde_json::Value;
use stencil_mcp::llmtransport::sanitize_detail;

mod common;
use common::walk::Walk;
use common::wire::load_array;

const SANITIZER_CASES: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../browser/js/config/llm/fixtures/sanitizer/cases.json"
);

static CORPUS: LazyLock<Vec<Value>> = LazyLock::new(|| load_array(SANITIZER_CASES));

fn check(case: &Value) {
    let name = case["name"].as_str().expect("case name");
    let input = case["input"].as_str().expect("a string input");
    let got = sanitize_detail(input);
    let want = common::overrides("sanitizer")[name]["expect"]
        .as_str()
        .or(case["expect"].as_str())
        .expect("an expected output");
    assert_eq!(got, want, "sanitized output");
    assert!(!got.contains("://"), "no URL may survive");
    assert!(got.chars().count() <= 201, "at most 200 chars + ellipsis");
}

fn main() {
    let mut walk = Walk::new();
    // `null` input is skipped: a Rust `&str` cannot be null, and the transport maps an
    // absent message to `""` before the sanitizer ever sees it.
    let cases: Vec<&Value> = CORPUS.iter().filter(|c| c["input"].is_string()).collect();
    walk.case("corpus_is_real", move || {
        assert!(cases.len() >= 15, "expected a real corpus, found {}", cases.len());
    });
    for case in CORPUS.iter().filter(|c| c["input"].is_string()) {
        walk.case(case["name"].as_str().expect("case name"), || check(case));
    }
    walk.run()
}
