//! A plan's several runs: §1 variant leniency — a misplaced top-level-only op costs the
//! VARIANT, not the turn — and the fan-out that executes what is left, against the slow
//! recording runner in `common::cli`.

mod common;
use common::cli::SlowCli;
use common::llm::{prompt_params, summary_and_payload, SequenceTransport, FIXTURE};

use std::time::Duration;

use stencil_mcp::config::Config;
use stencil_mcp::locate;
use stencil_mcp::opplan::EditRequest;
use stencil_mcp::server::{execute_plan, run_prompt};

#[tokio::test]
async fn a_variant_with_a_misplaced_op_is_dropped_and_the_rest_of_the_turn_is_delivered() {
    if locate::find_cli().is_err() {
        eprintln!("skipping e2e: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let transport = SequenceTransport::replying(&[
        r##"{"version":1,"reply":"Two takes.","actions":[{"op":"blank","color":"#ffffff","format":"a6"}],
            "variants":[{"label":"kept","actions":[{"op":"save","name":"p"}]},
                        {"label":"sepia","actions":[{"op":"filter","mode":"sepia"}]}]}"##,
    ]);

    let result = run_prompt(
        &Config::default(),
        transport.clone(),
        prompt_params("blank page, two takes", &dir.path().to_string_lossy()),
    )
    .await
    .unwrap();

    // The base actions and the well-formed variant both ran; the warning reaches the
    // caller as a note on the summary and in the payload, naming the dropped variant.
    let (summary, payload) = summary_and_payload(&result);
    assert_eq!(payload["reply"], "Two takes.");
    assert_eq!(payload["results"].as_array().unwrap().len(), 2);
    let note = payload["notes"][0].as_str().unwrap();
    assert!(
        note.contains("variant 1 (\"kept\")") && note.contains("top-level action only (§2.1)"),
        "{note}"
    );
    assert!(summary.contains(note), "{summary}");
}

#[tokio::test]
async fn a_plan_that_was_only_a_misplaced_variant_answers_with_a_reply_and_a_warning() {
    let dir = tempfile::tempdir().unwrap();
    let transport = SequenceTransport::replying(&[
        r##"{"version":1,"reply":"Saved it.","variants":[
            {"label":"saved","actions":[{"op":"save","name":"p"}]}]}"##,
    ]);

    let result = run_prompt(
        &Config::default(),
        transport.clone(),
        prompt_params("save it", &dir.path().to_string_lossy()),
    )
    .await
    .unwrap();

    // Nothing left to execute — a normal reply plus the warning, never a tool error.
    let (summary, payload) = summary_and_payload(&result);
    assert_eq!(payload["reply"], "Saved it.");
    assert!(payload["results"].as_array().unwrap().is_empty());
    let note = payload["notes"][0].as_str().unwrap();
    assert!(note.contains("variant 1 (\"saved\")"), "{note}");
    assert_eq!(summary, format!("Saved it.\n{note}"));
}

// ── the fan-out ──

/// One run of the plan, writing into the test's own directory.
fn request(label: Option<&str>, dir: &tempfile::TempDir, name: &str) -> EditRequest {
    let output = dir.path().join(name).to_string_lossy().into_owned();
    EditRequest {
        label: label.map(str::to_string),
        project: false,
        params: serde_json::from_value(serde_json::json!({"input": FIXTURE, "output": output}))
            .expect("params should deserialize"),
    }
}

/// The runs go out together and come back in REQUEST order whatever order they finish in —
/// here the first request is the slowest, so a completion-ordered reply would invert them.
#[tokio::test]
async fn the_plan_runs_overlap_and_keep_their_request_order() {
    let runner = SlowCli::with_delays(&[90, 60, 30]);
    let dir = tempfile::tempdir().unwrap();
    let requests = vec![
        request(None, &dir, "base.png"),
        request(Some("sepia"), &dir, "sepia.png"),
        request(Some("bw"), &dir, "bw.png"),
    ];

    let results = execute_plan(runner.clone(), requests).await.expect("every run succeeds");

    let labels: Vec<Option<String>> = results.iter().map(|r| r.label.clone()).collect();
    assert_eq!(labels, vec![None, Some("sepia".into()), Some("bw".into())]);
    assert!(results[0].path.ends_with("base.png"), "{}", results[0].path);
    assert!(results[2].path.ends_with("bw.png"), "{}", results[2].path);
    assert_eq!(runner.peak(), 3, "the runs were serialized, not fanned out");
    assert_eq!(runner.finished(), 3);
}

/// A cancelled tool call must take its CLI children with it. rmcp cancels by DROPPING the
/// tool future, so the runs have to be aborted with it — never left running detached.
#[tokio::test]
async fn a_dropped_call_aborts_the_runs_instead_of_detaching_them() {
    let runner = SlowCli::with_delays(&[400, 400]);
    let dir = tempfile::tempdir().unwrap();
    let requests = vec![request(None, &dir, "base.png"), request(Some("sepia"), &dir, "sepia.png")];

    let call = execute_plan(runner.clone(), requests);
    let cancelled = tokio::time::timeout(Duration::from_millis(80), call).await;

    assert!(cancelled.is_err(), "the runs should still have been in flight");
    assert_eq!(runner.started(), 2, "both runs had begun");
    tokio::time::sleep(Duration::from_millis(600)).await;
    assert_eq!(runner.finished(), 0, "an aborted run must never reach the end of its CLI call");
}
