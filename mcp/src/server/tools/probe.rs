//! `stencil_probe`: read an image's pixel dimensions.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;

use super::{err_result, ok_result};
use crate::args::ProbeParams;
use crate::pipeline;

pub async fn run(params: ProbeParams) -> Result<CallToolResult, McpError> {
    match pipeline::run_probe(&params.input).await {
        Ok((width, height)) => {
            let summary = format!("{width}x{height}");
            let payload = serde_json::json!({ "width": width, "height": height });
            ok_result(summary, payload)
        }
        Err(message) => Ok(err_result(message)),
    }
}
