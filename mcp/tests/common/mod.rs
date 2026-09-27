//! Helpers shared by the test binaries; each uses a subset, so the rest is dead code there.
#![allow(dead_code)]

pub mod cli;
pub mod dispatch;
pub mod e2e;
pub mod args;
pub mod llm;
pub mod plan;
pub mod rest;
pub mod walk;
pub mod wire;

use std::sync::LazyLock;

use serde_json::Value;
use stencil_mcp::opplan::{from_result, OpPlan, OpPlanError};

/// A plan typed from a core result document — what `--plan-check` answers for a reply that
/// already reads this way (normalized, defaults applied); `status` defaults to valid.
pub fn try_plan(result: &str) -> Result<OpPlan, OpPlanError> {
    let mut doc: Value = serde_json::from_str(result).expect("a result document");
    doc.as_object_mut().expect("an object").entry("status").or_insert("valid".into());
    from_result(&doc.to_string())
}

pub fn plan_of(result: &str) -> OpPlan {
    try_plan(result).unwrap()
}

/// Parse a JSON file, naming it if it is missing or malformed.
pub fn read_json(path: &str) -> Value {
    let raw = std::fs::read_to_string(path).unwrap_or_else(|e| panic!("cannot read {path}: {e}"));
    serde_json::from_str(&raw).unwrap_or_else(|e| panic!("{path} is not valid JSON: {e}"))
}

/// `tests/fixture_overrides.json` — the measured, mcp-side divergences from the shared
/// corpora. Read once per test binary, not once per test function.
pub fn overrides(family: &str) -> &'static Value {
    static ALL: LazyLock<Value> = LazyLock::new(|| {
        read_json(concat!(env!("CARGO_MANIFEST_DIR"), "/tests/fixture_overrides.json"))
    });
    &ALL[family]
}
