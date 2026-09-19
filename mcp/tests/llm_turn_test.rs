//! run_prompt over a recording transport: §7 auto-continuation and the §3.0 one-round rule.
//! Only the LLM is mocked — plans execute through the real CLI (e2e_test.rs's self-skip).

mod common;
use common::llm::{prompt_params, summary_and_payload, SequenceTransport, FIXTURE};

use stencil_mcp::config::Config;
use stencil_mcp::llm::edge_map_suffix;
use stencil_mcp::locate;
use stencil_mcp::server::run_prompt;

#[tokio::test]
async fn a_load_only_plan_continues_once_with_the_loaded_image_attached() {
    if locate::find_cli().is_err() {
        eprintln!("skipping e2e: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let transport = SequenceTransport::replying(&[
        r##"{"version":1,"reply":"Blank ready — drawing next.","actions":[{"op":"blank","color":"#ffffff","format":"a6"}]}"##,
        r##"{"version":1,"reply":"Tinted.","actions":[{"op":"filter","mode":"sepia"}]}"##,
    ]);

    let result = run_prompt(
        &Config::default(),
        transport.clone(),
        prompt_params("create a blank page and tint it", &dir.path().to_string_lossy()),
    )
    .await
    .unwrap();

    // Exactly one continuation: two requests, the second re-sending the SAME prompt with
    // the §7 note appended and the freshly rendered result attached for vision.
    assert_eq!(transport.call_count(), 2);
    let first = transport.body(0);
    assert_eq!(first["messages"][1]["content"], "create a blank page and tint it");
    assert!(first["messages"][1].get("images").is_none(), "round 1 had no image to attach");
    let second = transport.body(1);
    assert_eq!(
        second["messages"][1]["content"],
        "create a blank page and tint it\n\n[The working image is now the picture those \
         actions loaded — continue with it, using its real pixel size.]"
    );
    let images = second["messages"][1]["images"].as_array().expect("the loaded image rides");
    assert!(!images.is_empty());
    // The §7 edge map rides directly after the snapshot, with its suffix sentence.
    assert_eq!(images.len(), 2);
    assert!(second["messages"][0]["content"]
        .as_str()
        .unwrap()
        .ends_with(edge_map_suffix()));

    // Both replies reach the caller; the base result was written (and then re-written).
    let (summary, payload) = summary_and_payload(&result);
    assert_eq!(payload["reply"], "Blank ready — drawing next.\nTinted.");
    assert_eq!(payload["results"].as_array().unwrap().len(), 1);
    let path = payload["results"][0]["path"].as_str().unwrap();
    assert!(path.ends_with("result.png"), "{path}");
    assert!(std::fs::metadata(path).unwrap().len() > 0);
    assert!(summary.contains("wrote "), "{summary}");
}

/// §3.0: a layout-drawing turn is ONE model round. The queue holds a single body, so any
/// post-plan round would hit the connect error and fail the call.
#[tokio::test]
async fn a_layout_turn_is_exactly_one_model_round() {
    if locate::find_cli().is_err() {
        eprintln!("skipping e2e: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let transport = SequenceTransport::replying(&[
        r##"{"version":1,"reply":"Boxed.","actions":[{"op":"blank","color":"#ffffff","format":"a6"},
            {"op":"layout","lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}],"color":"#ff0000"}]}]}"##,
    ]);

    let result = run_prompt(
        &Config::default(),
        transport.clone(),
        prompt_params("blank page with a line", &dir.path().to_string_lossy()),
    )
    .await
    .unwrap();

    // The plan drew, so it committed to its coordinates: no continuation, no extra pass.
    assert_eq!(transport.call_count(), 1);

    // The reply is the answer, verbatim — no note about corrections or self-checks.
    let (summary, payload) = summary_and_payload(&result);
    assert_eq!(payload["reply"], "Boxed.");
    assert!(payload["notes"].as_array().unwrap().is_empty(), "{payload}");
    assert_eq!(payload["results"].as_array().unwrap().len(), 1);
    for word in ["correction", "self-check", "keeping the lines"] {
        assert!(!summary.contains(word), "{summary}");
    }
}

/// The same one-round rule with an INPUT image: the §7 snapshot + edge map ride on the
/// main turn (they are not a post-plan pass), and the drawing turn still ends there.
#[tokio::test]
async fn a_layout_turn_over_an_input_image_still_costs_one_round() {
    if locate::find_cli().is_err() {
        eprintln!("skipping e2e: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let transport = SequenceTransport::replying(&[
        r##"{"version":1,"reply":"Outlined.","actions":[{"op":"layout","lines":[
            {"points":[{"x":1,"y":2},{"x":3,"y":4}],"color":"#ff0000"}]}]}"##,
    ]);
    let mut params = prompt_params("outline it", &dir.path().to_string_lossy());
    params.input = Some(FIXTURE.to_string());

    let result = run_prompt(&Config::default(), transport.clone(), params)
        .await
        .unwrap();

    assert_eq!(transport.call_count(), 1);
    // §7 stays: the working image and its edge map rode on that single round.
    let images = transport.body(0)["messages"][1]["images"]
        .as_array()
        .expect("the input image rides")
        .len();
    assert_eq!(images, 2);
    assert!(transport.body(0)["messages"][0]["content"]
        .as_str()
        .unwrap()
        .ends_with(edge_map_suffix()));

    let (summary, payload) = summary_and_payload(&result);
    assert_eq!(payload["reply"], "Outlined.");
    assert!(payload["notes"].as_array().unwrap().is_empty(), "{payload}");
    assert!(summary.contains("wrote "), "{summary}");
}

#[tokio::test]
async fn a_load_only_continuation_answer_does_not_loop() {
    if locate::find_cli().is_err() {
        eprintln!("skipping e2e: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    // Round 2 answers with ANOTHER load-only plan: it executes normally and the turn
    // ends — a third request would hit the exhausted queue and error the whole call.
    let transport = SequenceTransport::replying(&[
        r##"{"version":1,"reply":"First blank.","actions":[{"op":"blank","color":"#ffffff","format":"a6"}]}"##,
        r##"{"version":1,"reply":"Second blank.","actions":[{"op":"blank","color":"#000000","format":"a6"}]}"##,
    ]);

    let result = run_prompt(
        &Config::default(),
        transport.clone(),
        prompt_params("blank it", &dir.path().to_string_lossy()),
    )
    .await
    .unwrap();

    assert_eq!(transport.call_count(), 2, "bounded to a single continuation");
    let (_, payload) = summary_and_payload(&result);
    assert_eq!(payload["reply"], "First blank.\nSecond blank.");
    // Round 2 rewrote the same base path — reported once, not twice.
    assert_eq!(payload["results"].as_array().unwrap().len(), 1);
}
