//! Suite floors for this crate. Rust has no test-registry reflection on stable, so the counts
//! are static scans: they catch deleted tests, a test file that stopped declaring any, and a
//! walker that lost its Cargo.toml entry — never a declared test that is built but not run.

use std::path::{Path, PathBuf};

use serde_json::Value;

/// Raise these when the suite grows a lot. An addition must never trip one; a collapse must.
const DECLARED_FLOOR: usize = 270;
const CORPUS_FLOOR: usize = 80;

/// The fixture corpora the `harness = false` walkers expand into one reported case each —
/// the generated bulk of the suite, which no scan of the sources can see.
const CORPORA: [&str; 8] = [
    "cli/testdata/outcome_fixtures.json",
    "browser/js/config/llm/fixtures/sanitizer/cases.json",
    "browser/js/config/fixtures/layout/payload.json",
    "browser/js/config/fixtures/layout/sparse.json",
    "browser/js/config/llm/fixtures/providerWire/ollama.json",
    "browser/js/config/llm/fixtures/providerWire/openai.json",
    "browser/js/config/llm/fixtures/providerWire/server.json",
    "browser/js/config/llm/fixtures/providerWire/httpErrors.json",
];

fn crate_dir() -> &'static Path {
    Path::new(env!("CARGO_MANIFEST_DIR"))
}

fn read(path: &Path) -> String {
    std::fs::read_to_string(path).unwrap_or_else(|e| panic!("cannot read {}: {e}", path.display()))
}

/// Every `.rs` under `dir`, recursively.
fn sources(dir: &Path, out: &mut Vec<PathBuf>) {
    let entries =
        std::fs::read_dir(dir).unwrap_or_else(|e| panic!("cannot read {}: {e}", dir.display()));
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            sources(&path, out);
        } else if path.extension().is_some_and(|e| e == "rs") {
            out.push(path);
        }
    }
}

/// `#[test]` / `#[tokio::test]` alone on a line — how every test in this crate is declared.
fn declared(src: &str) -> usize {
    src.lines().filter(|l| matches!(l.trim(), "#[test]" | "#[tokio::test]")).count()
}

/// A corpus is an array of cases, or an object whose values are such arrays.
fn cases(raw: &str) -> usize {
    // One browser `expect` ends in a lone high surrogate, which serde_json refuses; the
    // walkers neutralize the same escape (tests/common/wire.rs).
    let parsed: Value = serde_json::from_str(&raw.replace("\\ud83d", "\\ufffd"))
        .unwrap_or_else(|e| panic!("a corpus is not valid JSON: {e}"));
    match &parsed {
        Value::Array(all) => all.len(),
        Value::Object(sections) => {
            sections.values().filter_map(Value::as_array).map(Vec::len).sum()
        }
        _ => 0,
    }
}

#[test]
fn the_suite_still_declares_at_least_its_floor_of_tests() {
    let mut files = Vec::new();
    sources(&crate_dir().join("src"), &mut files);
    sources(&crate_dir().join("tests"), &mut files);
    assert!(files.len() >= 80, "the crate was not walked: only {} sources found", files.len());
    let total: usize = files.iter().map(|p| declared(&read(p))).sum();
    assert!(
        total >= DECLARED_FLOOR,
        "mcp suite collapsed to {total} declared tests, floor is {DECLARED_FLOOR}"
    );
}

/// A walker that loses its `harness = false` entry builds under libtest instead, never runs
/// its `fn main`, and reports zero tests while staying green.
#[test]
fn every_integration_target_declares_tests_or_is_a_registered_walker() {
    let manifest = read(&crate_dir().join("Cargo.toml"));
    let dir = crate_dir().join("tests");
    let (mut seen, mut problems) = (0, Vec::new());
    for entry in std::fs::read_dir(&dir).expect("the tests directory").flatten() {
        let path = entry.path();
        if !path.extension().is_some_and(|e| e == "rs") {
            continue;
        }
        seen += 1;
        let src = read(&path);
        if declared(&src) > 0 {
            continue;
        }
        let stem = path.file_stem().expect("a file name").to_string_lossy().to_string();
        let registered = manifest.contains(&format!("name = \"{stem}\"\nharness = false"));
        if !(registered && src.contains("\nfn main()")) {
            problems.push(stem);
        }
    }
    assert!(seen >= 40, "tests/ was not walked: only {seen} targets found");
    assert!(problems.is_empty(), "integration targets that run no test: {problems:?}");
}

#[test]
fn the_walked_fixture_corpora_still_hold_their_cases() {
    let root = crate_dir().parent().expect("the repo root above mcp/");
    let mut total = 0;
    for rel in CORPORA {
        let n = cases(&read(&root.join(rel)));
        assert!(n > 0, "{rel} holds no cases");
        total += n;
    }
    assert!(
        total >= CORPUS_FLOOR,
        "mcp fixture corpora collapsed to {total} cases, floor is {CORPUS_FLOOR}"
    );
}
