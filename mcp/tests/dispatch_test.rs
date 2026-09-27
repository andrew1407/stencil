//! `tools/call` dispatch: a real MCP call reaches the tool's own body. Every other suite
//! starts inside a tool; this drives the public `ServerHandler::call_tool`, so the generated
//! router, the parameter deserialization and the delegation to `server::tools::*` are all on
//! the path.

mod common;
use common::dispatch::{text_of, Harness};

use rmcp::model::PaginatedRequestParams;
use rmcp::ServerHandler;
use serde_json::json;

/// The router advertises exactly the tools `server/mod.rs` declares.
#[tokio::test]
async fn tools_list_reports_every_declared_tool() {
    let h = Harness::new();
    let listed = h
        .server
        .list_tools(Some(PaginatedRequestParams::default()), h.context())
        .await
        .expect("the router lists its tools");
    let mut names: Vec<&str> = listed.tools.iter().map(|t| t.name.as_ref()).collect();
    names.sort_unstable();
    assert_eq!(
        names,
        [
            "source_site",
            "stencil_edit",
            "stencil_probe",
            "stencil_project_file",
            "stencil_project_update",
            "stencil_projects",
            "stencil_prompt",
            "stencil_script",
            "stencil_script_check",
            "stencil_script_emit",
            "stencil_script_plan",
        ]
    );
}

/// Every write is fenced into the server's roots — here the working directory, with no
/// client roots and no STENCIL_MCP_ROOTS: an absolute path elsewhere is refused unrun.
#[tokio::test]
async fn a_stencil_edit_outside_the_roots_is_refused() {
    let h = Harness::new();
    let outside = "/stencil-mcp-no-such-root/escaped.png";
    let result = h
        .call("stencil_edit", json!({ "blank": { "width": 4, "height": 4 }, "output": outside }))
        .await
        .expect("a refusal is a tool error");

    assert_eq!(serde_json::to_value(&result).unwrap()["isError"], true);
    let message = text_of(&result);
    assert!(message.contains("outside the allowed roots"), "got: {message}");
    assert!(!std::path::Path::new(outside).exists());
}

/// With no operator allowlist, a model-chosen server URL is refused before anything runs.
#[tokio::test]
async fn a_server_url_without_the_operator_allowlist_is_refused() {
    let h = Harness::new();
    let server = "http://attacker.test:8090";
    let call = json!({ "input": "Plans", "server": server, "output": "p.png" });
    let result = h.call("stencil_edit", call).await.expect("a refusal is a tool error");

    let message = text_of(&result);
    assert!(message.contains("STENCIL_MCP_SERVERS"), "{message}");
    assert!(message.contains("none is configured"), "{message}");
}

/// A call the client already cancelled answers at once as a cancelled tool error.
#[tokio::test]
async fn a_cancelled_call_answers_as_cancelled() {
    let h = Harness::new();
    let context = h.context();
    context.ct.cancel();
    let png = concat!(env!("CARGO_MANIFEST_DIR"), "/../cli/tests/fixtures/sample.png");
    let request = serde_json::from_value(json!({
        "name": "stencil_probe",
        "arguments": { "input": png },
    }))
    .unwrap();
    let result = h.server.call_tool(request, context).await.expect("a tool result");

    assert_eq!(serde_json::to_value(&result).unwrap()["isError"], true);
    assert!(text_of(&result).contains("cancelled"), "got: {}", text_of(&result));
}

/// A success carries the payload twice: as a JSON text block and as `structuredContent`.
#[tokio::test]
async fn a_probe_answers_with_structured_content() {
    let h = Harness::new();
    let png = concat!(env!("CARGO_MANIFEST_DIR"), "/../cli/tests/fixtures/sample.png");
    let result = h.call("stencil_probe", json!({ "input": png })).await.expect("a result");

    let wire = serde_json::to_value(&result).unwrap();
    let structured = &wire["structuredContent"];
    assert_eq!((structured["width"].as_u64(), structured["height"].as_u64()), (Some(16), Some(12)));
    assert_eq!(structured["format"], "png");
    assert!(structured["bytes"].as_u64().is_some_and(|b| b > 0), "{structured}");
    assert_eq!(&common::dispatch::payload_of(&result), structured);
    assert!(text_of(&result).starts_with("16x12 · png"), "got: {}", text_of(&result));
}

/// A `tools/call` for `stencil_edit` runs the real body into `args::build_argv`: the unknown
/// page format is that builder's own message, reported as a tool error, with no CLI spawned.
#[tokio::test]
async fn a_stencil_edit_call_reaches_build_argv() {
    let h = Harness::new();
    let result = h
        .call(
            "stencil_edit",
            json!({ "blank": { "page": "Z9" }, "output": "/tmp/never-written.png" }),
        )
        .await
        .expect("a validation failure is a tool error, not a protocol error");

    assert_eq!(serde_json::to_value(&result).unwrap()["isError"], true);
    let message = text_of(&result);
    assert!(message.contains("not a known page format"), "got: {message}");
    assert!(!std::path::Path::new("/tmp/never-written.png").exists());
}

/// The same for `source_site`, whose body starts in the other argv builder.
#[tokio::test]
async fn a_source_site_call_reaches_the_scrape_argv_builder() {
    let h = Harness::new();
    let result = h
        .call("source_site", json!({ "source_site": "" }))
        .await
        .expect("a validation failure is a tool error");

    assert_eq!(serde_json::to_value(&result).unwrap()["isError"], true);
    let message = text_of(&result);
    assert!(message.contains("`source_site` must not be empty"), "got: {message}");
}

/// The same for `stencil_script`, whose body starts in the script parameter guards — so a
/// call with no script at all never writes a temp file and never spawns the CLI.
#[tokio::test]
async fn a_stencil_script_call_reaches_the_script_guards() {
    let h = Harness::new();
    let result = h
        .call("stencil_script", json!({ "output_dir": "/tmp/never-made" }))
        .await
        .expect("a validation failure is a tool error");

    assert_eq!(serde_json::to_value(&result).unwrap()["isError"], true);
    let message = text_of(&result);
    assert!(message.contains("no script"), "got: {message}");
    assert!(!std::path::Path::new("/tmp/never-made").exists());
}

/// Arguments that do not match the schema fail in the router, before any body runs.
#[tokio::test]
async fn arguments_missing_a_required_field_never_reach_the_tool() {
    let h = Harness::new();
    let result = h
        .call("stencil_edit", json!({ "input": "a.png" }))
        .await
        .expect("a bad-parameter call still answers");

    assert_eq!(serde_json::to_value(&result).unwrap()["isError"], true);
    assert_eq!(text_of(&result), "failed to deserialize parameters: missing field `output`");
}

#[tokio::test]
async fn an_unknown_tool_name_is_refused_by_the_router() {
    let h = Harness::new();
    let error = h.call("stencil_paste", json!({})).await.expect_err("no such tool");
    assert!(error.message.to_lowercase().contains("tool"), "got: {}", error.message);
}
