//! Replay the shared, language-neutral golden fixtures for the CLI stderr OUTPUT grammar
//! through `outcome.rs`'s parsers, one reported case per fixture. The SAME file
//! (`cli/testdata/outcome_fixtures.json`) is replayed by the .NET bot's
//! `SharedOutcomeFixturesTests`, so if the two parsers ever disagree on a case, one of the
//! suites goes red — that is the drift this catches. The per-parser unit cases still live in
//! `outcome_test.rs`; this asserts conformance to the canonical contract (`cli/CONTRACT.md`).

use std::sync::LazyLock;

use serde_json::Value;
use stencil_mcp::outcome::{extract_errors, parse_remotes, parse_wrote};

mod common;
use common::walk::Walk;

/// Resolved against this crate's manifest dir, so the working directory does not matter.
/// Read once per test binary.
static FIXTURES: LazyLock<Value> = LazyLock::new(|| {
    common::read_json(concat!(env!("CARGO_MANIFEST_DIR"), "/../cli/testdata/outcome_fixtures.json"))
});

fn cases(section: &str) -> &'static Vec<Value> {
    FIXTURES[section]
        .as_array()
        .unwrap_or_else(|| panic!("fixtures missing array section `{section}`"))
}

fn name(case: &Value) -> &str {
    case["name"].as_str().unwrap_or("<unnamed>")
}

fn stderr(case: &Value) -> &str {
    case["stderr"].as_str().expect("case.stderr must be a string")
}

fn check_wrote(case: &Value) {
    let got = parse_wrote(stderr(case));
    let expected = &case["expected"];
    if expected.is_null() {
        assert!(got.is_none(), "expected no success line, got {got:?}");
        return;
    }
    let w = got.expect("expected a wrote line, got none");
    assert_eq!(w.path, expected["path"].as_str().unwrap(), "path");
    assert_eq!(u64::from(w.width), expected["width"].as_u64().unwrap(), "width");
    assert_eq!(u64::from(w.height), expected["height"].as_u64().unwrap(), "height");
}

fn check_remotes(case: &Value) {
    // Remote derives Serialize with `#[serde(tag = "action", …)]`, producing exactly the
    // `{"action":…}` objects the fixtures encode — so compare as JSON values.
    let got = serde_json::to_value(parse_remotes(stderr(case))).unwrap();
    assert_eq!(got, case["expected"], "remotes");
}

fn check_errors(case: &Value) {
    assert_eq!(extract_errors(stderr(case)), case["expected"].as_str().unwrap(), "errors");
}

fn main() {
    let mut walk = Walk::new();
    let sections: [(&str, fn(&Value)); 3] =
        [("wrote", check_wrote), ("remotes", check_remotes), ("errors", check_errors)];
    for (section, check) in sections {
        for case in cases(section) {
            walk.case(format!("{section}/{}", name(case)), move || check(case));
        }
    }
    walk.run()
}
