//! Walk the shared op-plan conformance corpus (`browser/js/config/llm/fixtures/opPlan/`)
//! through the REAL mcp validator (`opplan::parse_op_plan`), one reported case per fixture.
//! Port of the reference walker
//! `browser/tests/opPlanFixtures.test.js` for the `mcp` profile — this PINS current
//! behavior; measured disagreements live in `tests/fixture_overrides.json`, never as
//! edits to the shared fixtures or to production code.

use std::sync::LazyLock;

use serde_json::Value;
use stencil_mcp::opplan::parse_op_plan;

mod common;
use common::walk::Walk;

const FIXTURES_DIR: &str =
    concat!(env!("CARGO_MANIFEST_DIR"), "/../browser/js/config/llm/fixtures/opPlan");

const PROFILES: [&str; 6] = ["editor", "console", "bot", "mcp", "extension", "all"];
const SURFACES: [&str; 7] = ["browser", "desktop", "cli", "pystencil", "bot", "mcp", "extension"];

/// The corpus as (file name, fixture) pairs, sorted. ~190 files plus a 212 KB generated
/// bundle — read once per test binary, not once per test function.
static CORPUS: LazyLock<Vec<(String, Value)>> = LazyLock::new(load_corpus);

fn load_corpus() -> Vec<(String, Value)> {
    let mut files: Vec<String> = std::fs::read_dir(FIXTURES_DIR)
        .unwrap_or_else(|e| panic!("cannot read the opPlan corpus at {FIXTURES_DIR}: {e}"))
        .filter_map(|entry| {
            let name = entry.ok()?.file_name().into_string().ok()?;
            name.ends_with(".json").then_some(name)
        })
        .collect();
    files.sort();
    let mut corpus: Vec<(String, Value)> = files
        .into_iter()
        .map(|file| {
            let raw = std::fs::read_to_string(format!("{FIXTURES_DIR}/{file}"))
                .unwrap_or_else(|e| panic!("cannot read {file}: {e}"));
            let fx: Value = serde_json::from_str(&raw)
                .unwrap_or_else(|e| panic!("{file} is not valid JSON: {e}"));
            (file, fx)
        })
        .collect();
    // The registry-generated bundle (browser/tools/genOpPlanFixtures.mjs): one pseudo-file per case.
    let bundle = format!("{FIXTURES_DIR}/generated/cases.json");
    let raw = std::fs::read_to_string(&bundle).unwrap_or_else(|e| panic!("cannot read {bundle}: {e}"));
    let generated: Value = serde_json::from_str(&raw).expect("generated/cases.json parses");
    for fx in generated["cases"].as_array().expect("cases array") {
        let name = fx["name"].as_str().expect("generated case name");
        corpus.push((format!("{name}.json"), fx.clone()));
    }
    corpus
}

/// The corpus-shape check: one case, and a malformed fixture names itself.
fn corpus_is_well_formed() {
    let corpus = &*CORPUS;
    assert!(
        corpus.len() >= 80,
        "expected a real corpus, found {} fixtures",
        corpus.len()
    );
    for (file, fx) in corpus {
        // Strip the NNN- prefix of a hand-written file; generated cases carry none.
        let slug = match file.split_once('-') {
            Some((num, rest)) if num.chars().all(|c| c.is_ascii_digit()) => rest,
            _ => file.as_str(),
        };
        assert_eq!(
            format!("{}.json", fx["name"].as_str().unwrap_or_default()),
            slug,
            "{file}: \"name\" must match the filename slug"
        );
        let profiles = fx["profiles"].as_array();
        assert!(
            profiles.is_some_and(|p| !p.is_empty()),
            "{file}: \"profiles\" must be a non-empty array"
        );
        for p in profiles.unwrap() {
            assert!(
                p.as_str().is_some_and(|p| PROFILES.contains(&p)),
                "{file}: unknown profile {p}"
            );
        }
        let expect = fx["expect"].as_str();
        assert!(
            matches!(expect, Some("valid" | "invalid")),
            "{file}: \"expect\" must be valid|invalid"
        );
        assert!(!fx["input"].is_null(), "{file}: \"input\" is required");
        if expect == Some("invalid") {
            assert!(
                fx["reason"].as_str().is_some_and(|r| !r.is_empty()),
                "{file}: invalid cases need a \"reason\""
            );
        }
        if let Some(kd) = fx.get("knownDivergence").and_then(Value::as_object) {
            for (surface, verdict) in kd {
                assert!(
                    SURFACES.contains(&surface.as_str()),
                    "{file}: unknown knownDivergence surface \"{surface}\""
                );
                assert!(
                    matches!(verdict.as_str(), Some("valid" | "invalid")),
                    "{file}: knownDivergence verdicts are valid|invalid"
                );
            }
        }
    }
}

fn applies(fx: &Value) -> bool {
    fx["profiles"]
        .as_array()
        .is_some_and(|p| p.iter().any(|p| matches!(p.as_str(), Some("mcp" | "all"))))
}

/// Verdict precedence: local override > knownDivergence.mcp > expect.
fn check_verdict(fx: &Value) {
    let name = fx["name"].as_str().unwrap_or_default();
    let want = common::overrides("opPlan")[name]["verdict"]
        .as_str()
        .or_else(|| fx["knownDivergence"]["mcp"].as_str())
        .or_else(|| fx["expect"].as_str())
        .expect("a verdict");
    // String input verbatim; object input serialized, as a model reply would arrive.
    let text = match &fx["input"] {
        Value::String(s) => s.clone(),
        other => serde_json::to_string(other).unwrap(),
    };
    let got = if parse_op_plan(&text).is_ok() { "valid" } else { "invalid" };
    assert_eq!(got, want, "expected {want}, mcp says {got}");
}

fn main() {
    let mut walk = Walk::new();
    walk.case("corpus_is_well_formed", corpus_is_well_formed);
    let walked = CORPUS.iter().filter(|(_, fx)| applies(fx)).count();
    walk.case("every_mcp_profile_fixture_is_walked", move || {
        assert!(walked >= 80, "expected many mcp-profile fixtures, walked {walked}");
    });
    for (file, fx) in CORPUS.iter().filter(|(_, fx)| applies(fx)) {
        walk.case(format!("mcp/{file}"), || check_verdict(fx));
    }
    walk.run()
}
