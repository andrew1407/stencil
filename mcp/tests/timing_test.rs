//! Opt-in benchmarks for the three hot paths: `cargo test -- --ignored`, out of CI because
//! no clock survives a loaded runner. Every assertion is RELATIVE — a ratio between two
//! sizes of the same call — so they catch an algorithmic regression, not tuning noise. The
//! signal is the ns/call each case prints; the README records today's numbers.

use std::time::{Duration, Instant};

use serde_json::json;
use stencil_mcp::args::{build_argv, EditParams};
use stencil_mcp::llmtransport::sanitize_detail;
use stencil_mcp::opplan::schema::{schema, JsonObject};

/// Batches per measurement; the best (min) one drops noise, as the core's `best_ms` does.
const REPS: u32 = 5;

/// Best per-call cost over `REPS` batches of `rounds`, printed and handed back.
fn per_call(name: &str, rounds: u32, mut body: impl FnMut()) -> Duration {
    body(); // one warm round: every path here parses the embedded registry lazily
    let mut best = Duration::MAX;
    for _ in 0..REPS {
        let started = Instant::now();
        for _ in 0..rounds {
            body();
        }
        best = best.min(started.elapsed() / rounds);
    }
    println!("{name}: {:>9} ns/call (best of {REPS} x {rounds})", best.as_nanos());
    best
}

fn ratio(bigger: Duration, smaller: Duration) -> f64 {
    bigger.as_secs_f64() / smaller.as_secs_f64()
}

fn object(value: serde_json::Value) -> JsonObject {
    value.as_object().expect("an object").clone()
}

/// A `layout` action carrying one line of `points` points.
fn layout_action(points: usize) -> JsonObject {
    let pts: Vec<_> = (0..points).map(|i| json!({ "x": i, "y": i * 2 })).collect();
    object(json!({
        "op": "layout",
        "lines": [{ "points": pts, "color": "#FF0000", "style": "dashed", "thickness": 2 }]
    }))
}

/// `validate_action` is the hottest path here — the whole fixture corpus walks through it.
/// Its cost must stay LINEAR: an 8x longer point list may not cost more than 8x, with room.
#[test]
#[ignore = "bench: run with --ignored"]
fn validate_action_scales_linearly_in_plan_size() {
    let s = schema();
    let layout = s.entry("layout").expect("layout is registered").clone();
    let crop_entry = s.entry("crop").expect("crop is registered").clone();
    let crop = object(json!({
        "op": "crop",
        "spec": { "x1": "10%", "x2": "-10%", "y1": "0", "y2": "1.5cm", "aspect": "16:9" }
    }));
    let (small, big) = (layout_action(64), layout_action(512));

    per_call("validate_action(crop)", 5_000, || {
        s.validate_action(&crop, &crop_entry).expect("the crop action is valid");
    });
    let small_cost = per_call("validate_action(layout, 64 points)", 500, || {
        s.validate_action(&small, &layout).expect("the small layout is valid");
    });
    let big_cost = per_call("validate_action(layout, 512 points)", 100, || {
        s.validate_action(&big, &layout).expect("the big layout is valid");
    });

    let grew = ratio(big_cost, small_cost);
    println!("validate_action: 8x the points cost {grew:.2}x");
    assert!(grew < 16.0, "8x the points cost {grew:.2}x — validation is no longer linear");
}

/// The sanitizer only ever scans the first 800 characters, so a 20x longer message must
/// cost barely more than a short one; scaling with the input means the window regressed.
#[test]
#[ignore = "bench: run with --ignored"]
fn sanitize_detail_is_bounded_by_its_scan_window() {
    let unit = "upstream https://api.internal.test/v1/chat failed: token sk-abcdefghijklmnop \
                rejected for model llama3.2-vision at 2026-01-01T00:00:00Z; ";
    let (short, long) = (unit.repeat(2), unit.repeat(40));
    assert!(short.len() > 200 && long.len() > 5_000, "both sides must exceed the window");

    let short_cost = per_call("sanitize_detail (280 chars)", 5_000, || {
        assert!(!sanitize_detail(&short).contains("://"));
    });
    let long_cost = per_call("sanitize_detail (5600 chars)", 5_000, || {
        assert!(!sanitize_detail(&long).contains("://"));
    });

    let grew = ratio(long_cost, short_cost);
    println!("sanitize_detail: 20x the input cost {grew:.2}x");
    assert!(grew < 4.0, "20x the input cost {grew:.2}x — the 800-char scan window regressed");
}

/// `build_argv` runs once per CLI invocation, and an op plan fans out to many. Cost tracks
/// the FLAG COUNT: a loaded call may not cost an order of magnitude more than a bare one.
#[test]
#[ignore = "bench: run with --ignored"]
fn build_argv_cost_tracks_the_flag_count() {
    let params = |value: serde_json::Value| -> EditParams {
        serde_json::from_value(value).expect("params should deserialize")
    };
    let bare = params(json!({ "input": "https://example.test/shot.png", "output": "/tmp/out.png" }));
    let loaded = params(json!({
        "input": "https://example.test/shot.png",
        "crop": { "x1": "10%", "x2": "-10%", "y1": "0", "y2": "-5%" },
        "rotate": 1,
        "filter": "sepia",
        "output": "/tmp/out.png",
        "overwrite": true,
    }));

    let bare_cost = per_call("build_argv (2 flags)", 100_000, || {
        build_argv(&bare, None).expect("the argv builds");
    });
    let loaded_cost = per_call("build_argv (8 flags + layout)", 100_000, || {
        build_argv(&loaded, Some("/tmp/layout.json")).expect("the argv builds");
    });

    let grew = ratio(loaded_cost, bare_cost);
    println!("build_argv: the loaded call cost {grew:.2}x the bare one");
    assert!(grew < 12.0, "the loaded call cost {grew:.2}x the bare one — argv building regressed");
}
