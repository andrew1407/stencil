//! `stencil_script_check`, `stencil_script_plan`, `stencil_script_emit` and a `.stencil`
//! `stencil_edit` end to end: real `tools/call`s through the real CLI, self-skipping
//! without the binary.
mod common;
use common::dispatch::{payload_of, text_of, wire, Harness};
use common::e2e::{cli_present, FIXTURE};

use serde_json::json;

#[tokio::test]
async fn a_check_reports_structured_diagnostics_without_running_anything() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let h = Harness::rooted(dir.path());
    let bad = h.call("stencil_script_check", json!({ "script_text": "@crp 10%\n" })).await.unwrap();

    assert_eq!(wire(&bad)["isError"], false, "a verdict is not a tool failure");
    let payload = payload_of(&bad);
    assert_eq!(payload["valid"], false);
    assert_eq!(payload["diagnostics"][0]["code"], "E_UNKNOWN_DIRECTIVE");
    assert_eq!(payload["diagnostics"][0]["line"], 1);

    let clean = json!({ "script_text": "@crop 10%\n@save o\n" });
    let good = h.call("stencil_script_check", clean).await;
    assert_eq!(payload_of(&good.unwrap())["valid"], true);
    assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 0, "a check writes nothing");
}

#[tokio::test]
async fn a_plan_lists_the_actions_and_saves_a_run_would_make() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let call = json!({ "script_text": "@crop 25%\n@save small\n", "input": FIXTURE });
    let result = Harness::rooted(dir.path()).call("stencil_script_plan", call).await.unwrap();

    assert_eq!(wire(&result)["isError"], false, "{}", text_of(&result));
    let plan = &wire(&result)["structuredContent"];
    assert_eq!(plan["script"], "<inline>");
    assert_eq!(plan["blocks"][0]["dims"], json!({ "width": 16, "height": 12 }));
    assert_eq!(plan["blocks"][0]["plans"][0]["actions"][0]["op"], "crop");
    assert_eq!(plan["blocks"][0]["saves"][0]["path"], "small.png");
    assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 0, "a plan writes nothing");
}

#[tokio::test]
async fn an_emit_writes_the_browser_script_inside_the_root() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let call = json!({ "script_text": "@crop 25%\n@save small\n", "output": "tour.stcjs" });
    let result = Harness::rooted(dir.path()).call("stencil_script_emit", call).await.unwrap();

    assert_eq!(wire(&result)["isError"], false, "{}", text_of(&result));
    let payload = payload_of(&result);
    assert_eq!(payload["target"], "javascript");
    assert_eq!(payload["path"], dir.path().join("tour.stcjs").to_string_lossy().as_ref());
    assert!(dir.path().join("tour.stcjs").is_file());
}

/// A `.stencil` output is a project: a success with no size, not a missing `wrote` line.
#[tokio::test]
async fn an_edit_to_a_stencil_output_bundles_a_project() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let call = json!({ "input": FIXTURE, "rotate": 1, "output": "shot.stencil" });
    let result = Harness::rooted(dir.path()).call("stencil_edit", call).await.unwrap();

    assert_eq!(wire(&result)["isError"], false, "{}", text_of(&result));
    let payload = payload_of(&result);
    assert_eq!((payload["width"].clone(), payload["height"].clone()), (json!(null), json!(null)));
    assert!(dir.path().join("shot.stencil").is_file());
    assert!(text_of(&result).ends_with("shot.stencil (project)"), "{}", text_of(&result));
}
