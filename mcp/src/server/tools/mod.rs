//! One file per tool: the whole body of each `#[tool]` method, as a free function the
//! method delegates to — plus the result wrappers and the output-schema helper they share.

use std::sync::Arc;

use rmcp::handler::server::tool::schema_for_output;
use rmcp::model::{CallToolResult, Content, JsonObject};
use rmcp::ErrorData as McpError;
use schemars::JsonSchema;
use serde::Serialize;

pub mod edit;
pub mod preview;
pub mod probe;
pub mod project_file;
pub mod project_update;
pub mod projects;
pub mod prompt;
pub mod script;
pub mod source_site;

/// A successful tool result: the text summary, the payload as a JSON text block for clients
/// that predate structured output, and the same payload as `structuredContent`.
fn ok_result(summary: String, payload: impl Serialize) -> Result<CallToolResult, McpError> {
    let structured = serde_json::to_value(&payload)
        .map_err(|e| McpError::internal_error(format!("unserializable result: {e}"), None))?;
    let blocks = vec![Content::text(summary), Content::json(&payload)?];
    let mut result = CallToolResult::success(blocks);
    result.structured_content = Some(structured);
    Ok(result)
}

/// Wrap a message as a tool error result.
pub(super) fn err_result(message: String) -> CallToolResult {
    CallToolResult::error(vec![Content::text(message)])
}

/// The `outputSchema` a payload type publishes.
pub(super) fn schema<T: JsonSchema + 'static>() -> Arc<JsonObject> {
    schema_for_output::<T>().unwrap_or_else(|e| panic!("an invalid output schema: {e}"))
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
        assert_eq!(wire["structuredContent"], json!({"path":"out.png"}));
    }

    #[test]
    fn err_result_is_flagged_and_carries_only_the_message() {
        let wire = wire(&err_result("output already exists".into()));
        assert_eq!(wire["isError"], true);
        assert_eq!(wire["content"].as_array().unwrap().len(), 1);
        assert_eq!(wire["content"][0]["text"], "output already exists");
    }
}
