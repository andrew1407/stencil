//! Opt-in timing floors for the three hot paths: `cargo test -- --ignored`. `#[ignore]`d
//! because CI is too noisy to gate on a clock. They catch an order-of-magnitude regression
//! only — the ceilings are a debug build's, ~10x a quiet laptop — and the signal is the
//! ns/call each one prints.

use std::time::{Duration, Instant};

use serde_json::json;
use stencil_mcp::args::{build_argv, EditParams};
use stencil_mcp::opplan::schema::{schema, JsonObject};
use stencil_mcp::llmtransport::sanitize_detail;

/// Run `body` `rounds` times, print the per-call cost, and hand it back.
fn per_call(name: &str, rounds: u32, mut body: impl FnMut()) -> Duration {
    // One warm round first: every path here parses the embedded registry lazily.
    body();
    let started = Instant::now();
    for _ in 0..rounds {
        body();
    }
    let each = started.elapsed() / rounds;
    println!("{name}: {:>9} ns/call over {rounds} calls", each.as_nanos());
    each
}

fn object(value: serde_json::Value) -> JsonObject {
    value.as_object().expect("an object").clone()
}

/// `validate_action` is the hottest thing in this suite — the whole fixture corpus walks
/// through it for every profile/surface pair.
#[test]
#[ignore = "timing: run with --ignored"]
fn validate_action_stays_fast() {
    let s = schema();
    let entry = s.entry("crop").expect("crop is registered").clone();
    let action = object(json!({
        "op": "crop",
        "spec": { "x1": "10%", "x2": "-10%", "y1": "0", "y2": "1.5cm", "aspect": "16:9" }
    }));

    let each = per_call("validate_action(crop)", 20_000, || {
        s.validate_action(&action, &entry).expect("the action is valid");
    });
    assert!(each < Duration::from_micros(200), "validate_action took {each:?}/call");
}

/// The sanitizer runs on every transport error, on text of unbounded length.
#[test]
#[ignore = "timing: run with --ignored"]
fn sanitize_detail_stays_fast() {
    let detail = "upstream https://api.internal.test/v1/chat failed: token sk-abcdefghijklmnop \
                  rejected for model llama3.2-vision at 2026-01-01T00:00:00Z; "
        .repeat(8);

    let each = per_call("sanitize_detail", 20_000, || {
        let out = sanitize_detail(&detail);
        assert!(!out.contains("://"));
    });
    assert!(each < Duration::from_micros(500), "sanitize_detail took {each:?}/call");
}

/// `build_argv` runs once per CLI invocation, and an op plan can fan out to many.
#[test]
#[ignore = "timing: run with --ignored"]
fn build_argv_stays_fast() {
    let params: EditParams = serde_json::from_value(json!({
        "input": "https://example.test/shot.png",
        "crop": { "x1": "10%", "x2": "-10%", "y1": "0", "y2": "-5%" },
        "rotate": 1,
        "filter": "sepia",
        "output": "/tmp/out.png",
        "overwrite": true,
    }))
    .expect("params should deserialize");

    let each = per_call("build_argv", 50_000, || {
        build_argv(&params, Some("/tmp/layout.json")).expect("the argv builds");
    });
    assert!(each < Duration::from_micros(100), "build_argv took {each:?}/call");
}
