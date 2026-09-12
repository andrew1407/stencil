//! `tools/call` dispatch: a real MCP call reaches the tool's own body. Every other suite
//! starts inside a tool; this drives the public `ServerHandler::call_tool`, so the generated
//! router, the parameter deserialization and the delegation to `server::tools::*` are all on
//! the path. The router is private, so the context's peer comes from `serve_directly` over a
//! dead pipe — nothing here ever calls back to a client.

use rmcp::model::{CallToolRequestParams, CallToolResult, PaginatedRequestParams, RequestId};
use rmcp::service::{serve_directly, RequestContext, RunningService};
use rmcp::{ErrorData, RoleServer, ServerHandler};
use serde_json::{json, Value};
use stencil_mcp::server::StencilServer;

/// A server with a live peer, so the request context a handler needs can be built.
struct Harness {
    server: StencilServer,
    running: RunningService<RoleServer, StencilServer>,
}

impl Harness {
    fn new() -> Harness {
        let server = StencilServer::default();
        let running = serve_directly(server.clone(), (tokio::io::empty(), tokio::io::sink()), None);
        Harness { server, running }
    }

    fn context(&self) -> RequestContext<RoleServer> {
        RequestContext::new(RequestId::Number(1), self.running.peer().clone())
    }

    async fn call(&self, name: &str, arguments: Value) -> Result<CallToolResult, ErrorData> {
        let request: CallToolRequestParams =
            serde_json::from_value(json!({ "name": name, "arguments": arguments }))
                .expect("a tools/call params object");
        self.server.call_tool(request, self.context()).await
    }
}

/// The text of a tool result's first content block.
fn text_of(result: &CallToolResult) -> String {
    serde_json::to_value(result).unwrap()["content"][0]["text"]
        .as_str()
        .expect("a text block")
        .to_string()
}

/// The router advertises exactly the four tools `server/mod.rs` declares.
#[tokio::test]
async fn tools_list_reports_the_four_tools() {
    let h = Harness::new();
    let listed = h
        .server
        .list_tools(Some(PaginatedRequestParams::default()), h.context())
        .await
        .expect("the router lists its tools");
    let mut names: Vec<&str> = listed.tools.iter().map(|t| t.name.as_ref()).collect();
    names.sort_unstable();
    assert_eq!(names, ["source_site", "stencil_edit", "stencil_probe", "stencil_prompt"]);
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
