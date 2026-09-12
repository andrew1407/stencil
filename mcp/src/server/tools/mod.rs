//! One file per tool: the whole body of each `#[tool]` method, as a free function the
//! method delegates to — plus the two result wrappers they all share.

use rmcp::model::{CallToolResult, Content};
use rmcp::ErrorData as McpError;
use serde::Serialize;

pub mod edit;
pub mod probe;
pub mod prompt;
pub mod source_site;

/// Wrap a text summary + JSON payload as a successful tool result.
fn ok_result(summary: String, payload: impl Serialize) -> Result<CallToolResult, McpError> {
    Ok(CallToolResult::success(vec![
        Content::text(summary),
        Content::json(payload)?,
    ]))
}

/// Wrap a message as a tool error result.
fn err_result(message: String) -> CallToolResult {
    CallToolResult::error(vec![Content::text(message)])
}

#[cfg(test)]
mod tests {
    //! The shared result wrappers (pure) — the contract with a calling agent, so every
    //! assertion runs against the real wire shape.

    use super::*;
    use crate::server::testwire::{payload_of, wire};
    use serde_json::json;

    #[test]
    fn ok_result_carries_a_text_summary_then_the_json_payload() {
        let result = ok_result("wrote out.png (2x2)".into(), json!({"path":"out.png"})).unwrap();
        let wire = wire(&result);

        assert_eq!(wire["isError"], false);
        assert_eq!(wire["content"].as_array().unwrap().len(), 2);
        assert_eq!(wire["content"][0]["type"], "text");
        assert_eq!(wire["content"][0]["text"], "wrote out.png (2x2)");
        assert_eq!(payload_of(&result), json!({"path":"out.png"}));
    }

    #[test]
    fn err_result_is_flagged_and_carries_only_the_message() {
        let wire = wire(&err_result("output already exists".into()));
        assert_eq!(wire["isError"], true);
        assert_eq!(wire["content"].as_array().unwrap().len(), 1);
        assert_eq!(wire["content"][0]["text"], "output already exists");
    }
}
