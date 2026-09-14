//! The `tools/call` harness: a server with a live peer, so a test can drive the public
//! `ServerHandler::call_tool` the way an MCP client does. The peer speaks over a dead pipe —
//! nothing here ever calls back to a client.

use rmcp::model::{CallToolRequestParams, CallToolResult, RequestId};
use rmcp::service::{serve_directly, RequestContext, RunningService};
use rmcp::{ErrorData, RoleServer, ServerHandler};
use serde_json::{json, Value};
use stencil_mcp::server::StencilServer;

pub struct Harness {
    pub server: StencilServer,
    running: RunningService<RoleServer, StencilServer>,
}

impl Harness {
    pub fn new() -> Harness {
        let server = StencilServer::default();
        let running = serve_directly(server.clone(), (tokio::io::empty(), tokio::io::sink()), None);
        Harness { server, running }
    }

    pub fn context(&self) -> RequestContext<RoleServer> {
        RequestContext::new(RequestId::Number(1), self.running.peer().clone())
    }

    pub async fn call(&self, name: &str, arguments: Value) -> Result<CallToolResult, ErrorData> {
        let request: CallToolRequestParams =
            serde_json::from_value(json!({ "name": name, "arguments": arguments }))
                .expect("a tools/call params object");
        self.server.call_tool(request, self.context()).await
    }
}

/// The JSON an MCP client would see for a tool result.
pub fn wire(result: &CallToolResult) -> Value {
    serde_json::to_value(result).expect("a tool result serializes")
}

/// The text of a tool result's first content block.
pub fn text_of(result: &CallToolResult) -> String {
    wire(result)["content"][0]["text"].as_str().expect("a text block").to_string()
}

/// The structured payload: the second block, whose text is serialized JSON.
pub fn payload_of(result: &CallToolResult) -> Value {
    let wire = wire(result);
    let raw = wire["content"][1]["text"].as_str().expect("a JSON payload block");
    serde_json::from_str(raw).expect("the payload block holds valid JSON")
}
