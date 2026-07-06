//! End-to-end tests that drive the real CLI. They self-skip when the `stencil` binary
//! isn't built/findable, so `cargo test` stays green without a Zig toolchain present.

use serde_json::json;
use stencil_mcp::args::EditParams;
use stencil_mcp::{locate, pipeline};

/// The 16x12 PNG fixture shared with the CLI's own test suite.
const FIXTURE: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../cli/tests/fixtures/sample.png"
);

fn cli_present() -> bool {
    if locate::find_cli().is_err() {
        eprintln!("skipping e2e: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return false;
    }
    true
}

fn edit_params(value: serde_json::Value) -> EditParams {
    serde_json::from_value(value).expect("params should deserialize")
}

#[tokio::test]
async fn probe_reports_fixture_dimensions() {
    if !cli_present() {
        return;
    }
    let (w, h) = pipeline::run_probe(FIXTURE)
        .await
        .expect("probe should succeed");
    assert_eq!((w, h), (16, 12));
}

#[tokio::test]
async fn edit_rotate_swaps_dimensions() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let out = dir.path().join("rotated.png");
    let params = edit_params(json!({
        "input": FIXTURE,
        "rotate": 1,
        "output": out.to_string_lossy(),
    }));

    let result = pipeline::run_edit(&params)
        .await
        .expect("edit should succeed");
    assert_eq!((result.width, result.height), (12, 16));
    assert!(std::path::Path::new(&result.path).exists());
}

#[tokio::test]
async fn edit_crop_and_filter_with_inline_layout() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let out = dir.path().join("edited.png");
    let params = edit_params(json!({
        "input": FIXTURE,
        "crop": { "x1": "0", "x2": "50%" },
        "filter": "bw",
        "layout": {
            "lines": [ { "points": [ { "x": 0, "y": 0 }, { "x": 8, "y": 12 } ], "color": "#ff0000" } ]
        },
        "output": out.to_string_lossy(),
    }));

    // The crop halves the width; exact pixel counts are the core's crop math, so we only
    // assert the wrapper passed the args through and parsed a sane, shrunken result.
    let result = pipeline::run_edit(&params)
        .await
        .expect("edit should succeed");
    assert!(
        result.width < 16 && result.width > 0,
        "width = {}",
        result.width
    );
    assert!(
        result.height > 0 && result.height <= 12,
        "height = {}",
        result.height
    );
    assert!(std::path::Path::new(&result.path).exists());
}

#[tokio::test]
async fn refuses_to_clobber_without_overwrite() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let out = dir.path().join("exists.png");
    std::fs::write(&out, b"placeholder").unwrap();

    let params = edit_params(json!({
        "input": FIXTURE,
        "output": out.to_string_lossy(),
    }));
    let err = pipeline::run_edit(&params).await.unwrap_err().to_string();
    assert!(err.contains("already exists"), "got: {err}");
}
