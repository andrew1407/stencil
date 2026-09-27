//! `stencil_probe`: what the CLI's `--probe` reads of an image or video without decoding it,
//! or — only when no CLI binary can be found — what a local header says on its own.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};

use super::{err_result, ok_result};
use crate::args::{self, ProbeParams};
use crate::confine::Roots;
use crate::{imagesize, locate, pipeline};

/// The `stencil_probe` payload, as the CLI's `--probe` reports it (`cli/CONTRACT.md` §6).
/// `format`, `alpha` and `bytes` are null where unknown; `duration_ms` and `frames` are a
/// video's, null for a still.
#[derive(Serialize, Deserialize, JsonSchema)]
pub struct ProbePayload {
    width: u32,
    height: u32,
    #[serde(default)]
    format: Option<String>,
    #[serde(default)]
    alpha: Option<bool>,
    #[serde(default)]
    bytes: Option<u64>,
    #[serde(default, alias = "durationMs")]
    duration_ms: Option<u64>,
    #[serde(default)]
    frames: Option<u64>,
}

pub async fn run(roots: &Roots, params: ProbeParams) -> Result<CallToolResult, McpError> {
    let input = roots.resolve(&params.input);
    if let Err(missing) = locate::find_cli() {
        return match header_only(&input).await {
            Some(payload) => ok_result(summary(&payload), payload),
            None => Ok(err_result(missing)),
        };
    }
    let argv = match args::build_probe_argv(&input) {
        Ok(argv) => argv,
        Err(error) => return Ok(err_result(error.to_string())),
    };
    let document = match pipeline::run_probe(&argv).await {
        Ok(document) => document,
        Err(message) => return Ok(err_result(message)),
    };
    let payload: ProbePayload = match serde_json::from_value(document) {
        Ok(payload) => payload,
        Err(e) => return Ok(err_result(format!("the CLI's probe did not parse: {e}"))),
    };
    ok_result(summary(&payload), payload)
}

/// A local still's own header, for a host with no CLI binary: no video, no URL.
async fn header_only(input: &str) -> Option<ProbePayload> {
    let info = imagesize::read_info(input).await?;
    let bytes = std::fs::metadata(input).ok().map(|m| m.len());
    Some(ProbePayload {
        width: info.width,
        height: info.height,
        format: Some(if info.format == "jpeg" { "jpg" } else { info.format }.to_string()),
        alpha: info.alpha,
        bytes,
        duration_ms: None,
        frames: None,
    })
}

/// `WxH · format · alpha · bytes`, then a video's length.
fn summary(p: &ProbePayload) -> String {
    let mut text = format!("{}x{}", p.width, p.height);
    if let Some(format) = &p.format {
        text.push_str(&format!(" · {format}"));
    }
    match p.alpha {
        Some(true) => text.push_str(" · alpha"),
        Some(false) => text.push_str(" · no alpha"),
        None => {}
    }
    if let Some(bytes) = p.bytes {
        text.push_str(&format!(" · {bytes} bytes"));
    }
    if let Some(ms) = p.duration_ms {
        text.push_str(&format!(" · {ms} ms"));
    }
    if let Some(frames) = p.frames {
        text.push_str(&format!(" · {frames} frames"));
    }
    text
}
