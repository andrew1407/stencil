//! Shared test helpers: read a `CallToolResult` back as the exact JSON an MCP client
//! receives (`CallToolResult` serializes to the real wire shape).

use rmcp::model::CallToolResult;
use serde_json::Value;

/// The JSON an MCP client would see for a tool result.
pub fn wire(result: &CallToolResult) -> Value {
    serde_json::to_value(result).expect("a tool result serializes")
}

/// The human-readable summary: the first content block.
pub fn summary_of(result: &CallToolResult) -> String {
    wire(result)["content"][0]["text"]
        .as_str()
        .expect("the first block is the text summary")
        .to_string()
}

/// The structured payload: the second block, whose text is serialized JSON.
pub fn payload_of(result: &CallToolResult) -> Value {
    let raw = wire(result)["content"][1]["text"]
        .as_str()
        .expect("the second block is the JSON payload")
        .to_string();
    serde_json::from_str(&raw).expect("the payload block holds valid JSON")
}
