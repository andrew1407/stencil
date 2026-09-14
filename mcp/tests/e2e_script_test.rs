//! `stencil_script` end to end: a real `tools/call` whose inline source becomes a temp
//! `.stc` and runs through the real CLI. Self-skips when the binary is not found.
mod common;
use common::dispatch::{payload_of, text_of, wire, Harness};
use common::e2e::{cli_present, FIXTURE};

use serde_json::json;

/// The 16x12 fixture inset 25% on every side is 8x6, twice over.
const TWO_SAVES: &str = "@crop 25%\n@save small\n@filter bw\n@save small-bw\n";

fn call(script: &str, dir: &std::path::Path) -> serde_json::Value {
    json!({ "script_text": script, "input": FIXTURE, "output_dir": dir })
}

#[tokio::test]
async fn an_inline_script_runs_through_the_cli_and_reports_every_save() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let result = Harness::new()
        .call("stencil_script", call(TWO_SAVES, dir.path()))
        .await
        .expect("the tool answers");

    assert_eq!(wire(&result)["isError"], false, "got: {}", text_of(&result));
    let payload = payload_of(&result);
    let files = payload["files"].as_array().expect("a files array");
    assert_eq!(files.len(), 2, "got: {payload}");
    for (file, name) in files.iter().zip(["small.png", "small-bw.png"]) {
        let expected = dir.path().join(name);
        assert_eq!(file["path"], expected.to_string_lossy().as_ref());
        assert_eq!((file["width"].as_u64(), file["height"].as_u64()), (Some(8), Some(6)));
        assert!(expected.exists(), "{name} landed inside output_dir");
    }
    assert!(text_of(&result).contains("the script wrote 2 file(s)"));
}

/// Rule 3 through the real binary: `--confine-output` is what refuses a climbing `@save`.
#[tokio::test]
async fn a_save_that_climbs_out_of_the_output_directory_is_refused() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let nested = dir.path().join("inside");
    let result = Harness::new()
        .call("stencil_script", call("@filter bw\n@save ../escaped\n", &nested))
        .await
        .expect("a refused run is a tool error, not a protocol error");

    assert_eq!(wire(&result)["isError"], true);
    assert!(text_of(&result).contains("may not climb out"), "got: {}", text_of(&result));
    assert!(!dir.path().join("escaped.png").exists());
}

/// A script with a diagnostic runs nothing and the CLI's `[CODE]` lines are the tool error.
#[tokio::test]
async fn a_script_with_an_error_writes_nothing_and_reports_the_diagnostic() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let result = Harness::new()
        .call("stencil_script", call("@cropp 25%\n@save out\n", dir.path()))
        .await
        .expect("a failed run is a tool error");

    assert_eq!(wire(&result)["isError"], true);
    let message = text_of(&result);
    assert!(message.contains("[E_UNKNOWN_DIRECTIVE]"), "got: {message}");
    assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 0);
}
