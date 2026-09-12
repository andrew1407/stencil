//! The three §6 wire mappings, one per file, behind one trait.
//!
//! [`ProviderMapping`] turns the canonical request into a provider's shape and its reply
//! back into text. Adding a provider is a table entry plus its own file — never an edit to
//! [`super::chat`].

use serde::Serialize;
use serde_json::Value;

use super::{llm_system_prompt, ChatError, ChatMessage, LlmConfig, Provider};
use crate::llmtransport::{clip, SNIPPET_LEN};

mod ollama;
mod openai;
mod server;

/// One chat round on the wire: the URL, the extra headers, and the JSON body.
pub(super) type Request = (String, Vec<(String, String)>, String);

/// One provider's wire mapping (contract §6).
pub(super) trait ProviderMapping {
    /// Map the system prompt + messages onto this provider's request.
    fn request(&self, config: &LlmConfig, system: &str, messages: &[ChatMessage]) -> Request;

    /// Pull the reply text out of this provider's parsed 2xx body (`raw` is quoted in
    /// errors).
    fn reply(&self, value: &Value, raw: &str) -> Result<String, ChatError>;
}

/// The mapping for a provider.
fn mapping_for(provider: Provider) -> &'static dyn ProviderMapping {
    match provider {
        Provider::Ollama => &ollama::Ollama,
        Provider::OpenaiCompat => &openai::OpenaiCompat,
        Provider::StencilServer => &server::StencilServer,
    }
}

/// Map the messages onto the provider's wire shape: `(url, extra headers, JSON body)`.
pub(super) fn build_request(
    config: &LlmConfig,
    messages: &[ChatMessage],
    system_suffix: &str,
) -> Request {
    // §4: the canonical prompt verbatim, then the dynamic suffix (nothing is prepended).
    let base = llm_system_prompt();
    let system: std::borrow::Cow<'_, str> = if system_suffix.is_empty() {
        std::borrow::Cow::Borrowed(base)
    } else {
        std::borrow::Cow::Owned(format!("{base}\n\n{system_suffix}"))
    };
    mapping_for(config.provider).request(config, &system, messages)
}

/// Pull the reply text out of a provider's 2xx response body.
pub(super) fn extract_reply(provider: Provider, body: &str) -> Result<String, ChatError> {
    let value: Value = serde_json::from_str(body).map_err(|e| {
        ChatError::BadReply(format!(
            "the provider returned non-JSON ({e}): {}",
            clip(body, SNIPPET_LEN)
        ))
    })?;
    mapping_for(provider).reply(&value, body)
}

// The provider wire shapes (§6), as structs of borrows so building a request never copies
// message text or base64 image data — the only copy is the final serialization.

/// §6.1/§6.2 request body: `{"model", "stream": false, "messages"}`.
#[derive(Serialize)]
struct ChatBody<'a, M: Serialize> {
    model: &'a str,
    stream: bool,
    messages: Vec<M>,
}

/// `Authorization: Bearer <token>` when `token` is non-empty; no extra headers otherwise.
fn bearer(token: &str) -> Vec<(String, String)> {
    if token.is_empty() {
        Vec::new()
    } else {
        vec![("Authorization".to_string(), format!("Bearer {token}"))]
    }
}

fn to_json(body: &impl Serialize) -> String {
    serde_json::to_string(body).expect("the wire body serializes")
}
