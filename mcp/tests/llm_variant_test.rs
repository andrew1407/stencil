//! §1 variant leniency: a misplaced top-level-only op costs the VARIANT, not the turn.

mod common;
use common::llm::{prompt_params, summary_and_payload, SequenceTransport};

use stencil_mcp::config::Config;
use stencil_mcp::locate;
use stencil_mcp::server::run_prompt;

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
