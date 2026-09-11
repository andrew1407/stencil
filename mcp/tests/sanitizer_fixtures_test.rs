//! `browser/js/config/llm/fixtures/sanitizer/cases.json` against `sanitize_detail`. Where
//! mcp's word-based sanitizer differs from the browser's, `tests/fixture_overrides.json`
//! pins mcp's own output; every output keeps the no-URL and bounded-length invariants.
use stencil_mcp::llmtransport::sanitize_detail;

mod common;
use common::wire::load_array;

const SANITIZER_CASES: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../browser/js/config/llm/fixtures/sanitizer/cases.json"
);

// ── sanitizer ──

/// `cases.json` against `sanitize_detail`. `null` input is skipped (a Rust `&str` cannot
/// be null; the transport maps an absent message to `""` before the sanitizer). Every
/// output — pinned or shared — must keep the no-URL and bounded-length invariants.
#[test]
fn sanitizer_cases() {
    let overrides = common::overrides("sanitizer");
    let mut walked = 0usize;
    let mut failures: Vec<String> = Vec::new();
    for case in load_array(SANITIZER_CASES) {
        let name = case["name"].as_str().expect("case name");
        let Some(input) = case["input"].as_str() else {
            continue; // null input: unrepresentable here, skip per the schema
        };
        walked += 1;
        let got = sanitize_detail(input);
        let want = overrides[name]["expect"].as_str().or(case["expect"].as_str()).unwrap();
        if got != want {
            failures.push(format!("[{name}]\n got: {got:?}\nwant: {want:?}"));
        }
        assert!(!got.contains("://"), "[{name}] no URL may survive");
        assert!(got.chars().count() <= 201, "[{name}] at most 200 chars + ellipsis");
    }
    assert!(walked >= 15, "expected a real corpus, walked {walked}");
    assert!(failures.is_empty(), "sanitizer mismatches:\n{}", failures.join("\n"));
}
