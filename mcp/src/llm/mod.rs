//! The LLM client: provider configuration, the canonical system prompt, and the three
//! provider wire mappings of `llm-contract.md` (§4 prompt, §5 config, §6 wire).
//!
//! Providers: `ollama`, `openai-compat` and `stencil-server`. Requests go through the
//! [`crate::llmtransport`] trait; this module never touches pixels.

use serde_json::Value;

use crate::llmtransport::{LlmError, LlmTransport};
use crate::registry;

mod attach;
mod config;
mod error;
mod message;
mod providers;

pub use attach::{attach_local_image, edge_map_attachment};
pub use config::{LlmConfig, Provider};
pub use error::ChatError;
pub use message::{ChatMessage, ImageAttachment, Role};
use providers::{build_request, extract_reply};

/// The canonical cross-surface prompt asset (see `browser/js/config/llm/README.md`),
/// embedded at compile time — every shared sentence this surface speaks comes from it.
static PROMPT_ASSET: std::sync::LazyLock<Value> = std::sync::LazyLock::new(|| {
    serde_json::from_str(include_str!("../../../browser/js/config/llm/systemPrompt.json"))
        .expect("canonical browser/js/config/llm/systemPrompt.json is not valid JSON")
});

/// One string field of that asset, verbatim.
pub fn prompt_field(key: &str) -> &'static str {
    PROMPT_ASSET[key]
        .as_str()
        .unwrap_or_else(|| panic!("systemPrompt.json: \"{key}\" must be a string"))
}

/// The canonical system prompt — contract §4: the verbatim prose core around an ops
/// section generated from the op registry (§13), assembled once at first use.
pub fn llm_system_prompt() -> &'static str {
    static PROMPT: std::sync::OnceLock<String> = std::sync::OnceLock::new();
    PROMPT.get_or_init(|| {
        let ops =
            crate::registry::assemble_ops_section(registry::op_registry(), registry::WIRED_CAPABILITIES)
                .expect("§4 ops-section assembly from the op registry");
        // §4: the prose core (`head` ends at the "Available ops" heading, `tail` follows
        // the bullets) around the ops section generated from the registry.
        format!("{}{ops}{}", prompt_field("head"), prompt_field("tail"))
    })
}

/// Attachments larger than this are sent text-only with a note (this adapter is codec-free,
/// so it cannot downscale; the cap keeps payloads sane).
pub const MAX_IMAGE_BYTES: u64 = 8 * 1024 * 1024;

/// Contract §7: appended to the system-prompt suffix when — and only when — the edge map
/// is actually attached.
pub fn edge_map_suffix() -> &'static str {
    prompt_field("edgeMapSentence")
}

// ── Chat ──

/// Send one chat completion: build the provider request (§6), POST it through `transport`,
/// and extract the reply text. `system_suffix` is the §4 dynamic suffix.
pub fn chat(
    transport: &dyn LlmTransport,
    config: &LlmConfig,
    messages: &[ChatMessage],
    system_suffix: &str,
) -> Result<String, ChatError> {
    let (url, headers, body) = build_request(config, messages, system_suffix);
    let response = transport.post_json(&url, &headers, &body).map_err(|e| match e {
        // Keyed on the body's code like every client (browser 'disabled' parity).
        LlmError::Status { reason, code, .. } if code == "llmDisabled" => {
            ChatError::Disabled(reason)
        }
        other => ChatError::Transport(other),
    })?;
    extract_reply(config.provider, &response)
}

