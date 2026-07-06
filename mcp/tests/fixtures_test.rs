//! Replay the shared, language-neutral golden fixtures for the CLI stderr OUTPUT grammar
//! through `outcome.rs`'s parsers. The SAME file
//! (`cli/testdata/outcome_fixtures.json`) is replayed by the .NET bot's
//! `SharedOutcomeFixturesTests`, so if the two parsers ever disagree on a case, one of the
//! suites goes red — that is the drift this catches. The per-parser unit cases still live in
//! `outcome_test.rs`; this asserts conformance to the canonical contract (`cli/CONTRACT.md`).

use serde_json::Value;
use stencil_mcp::outcome::{extract_errors, parse_remotes, parse_wrote};

/// Load and parse the shared fixture file, resolved relative to this crate's manifest dir
/// (`mcp/`) so it works regardless of the test's working directory.
fn load_fixtures() -> Value {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../cli/testdata/outcome_fixtures.json");
    let raw = std::fs::read_to_string(path)
        .unwrap_or_else(|e| panic!("cannot read shared fixtures at {path}: {e}"));
    serde_json::from_str(&raw)
        .unwrap_or_else(|e| panic!("shared fixtures are not valid JSON: {e}"))
}

fn cases<'a>(fixtures: &'a Value, section: &str) -> &'a Vec<Value> {
    fixtures[section]
        .as_array()
        .unwrap_or_else(|| panic!("fixtures missing array section `{section}`"))
}

fn name(case: &Value) -> &str {
    case["name"].as_str().unwrap_or("<unnamed>")
}

fn stderr(case: &Value) -> &str {
    case["stderr"].as_str().expect("case.stderr must be a string")
}

#[test]
fn wrote_fixtures_match() {
    let fixtures = load_fixtures();
    for case in cases(&fixtures, "wrote") {
        let got = parse_wrote(stderr(case));
        let expected = &case["expected"];
        if expected.is_null() {
            assert!(
                got.is_none(),
                "[{}] expected no success line, got {got:?}",
                name(case)
            );
        } else {
            let w = got.unwrap_or_else(|| panic!("[{}] expected a wrote line, got none", name(case)));
            assert_eq!(w.path, expected["path"].as_str().unwrap(), "[{}] path", name(case));
            assert_eq!(u64::from(w.width), expected["width"].as_u64().unwrap(), "[{}] width", name(case));
            assert_eq!(u64::from(w.height), expected["height"].as_u64().unwrap(), "[{}] height", name(case));
        }
    }
}

#[test]
fn remote_fixtures_match() {
    let fixtures = load_fixtures();
    for case in cases(&fixtures, "remotes") {
        let got = parse_remotes(stderr(case));
        // Remote derives Serialize with `#[serde(tag = "action", …)]`, producing exactly the
        // `{"action":…}` objects the fixtures encode — so compare as JSON values.
        let got_json = serde_json::to_value(&got).unwrap();
        assert_eq!(got_json, case["expected"], "[{}] remotes", name(case));
    }
}

#[test]
fn error_fixtures_match() {
    let fixtures = load_fixtures();
    for case in cases(&fixtures, "errors") {
        let got = extract_errors(stderr(case));
        assert_eq!(got, case["expected"].as_str().unwrap(), "[{}] errors", name(case));
    }
}
