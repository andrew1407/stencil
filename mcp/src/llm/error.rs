//! Everything a chat round can fail with.

use crate::llmtransport::LlmError;

/// Everything `chat` can fail with. Hand-written, `Display` is the user-facing message.
#[derive(Debug)]
pub enum ChatError {
    /// The HTTP layer failed (scheme/connect/read/status).
    Transport(LlmError),
    /// stencil-server `stopReason: "max_tokens"` — the reply is truncated and per contract
    /// must NOT be parsed as a plan.
    Truncated,
    /// stencil-server `stopReason: "refusal"` — shown as an error, never parsed as a plan.
    Refusal(String),
    /// The provider answered 2xx but not in the documented response shape.
    BadReply(String),
    /// The server's 503 llmDisabled: no LLM key configured — a configure hint, not a
    /// broken transport. Carries the server's own (sanitized) message.
    Disabled(String),
}

impl std::fmt::Display for ChatError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ChatError::Transport(e) => e.fmt(f),
            ChatError::Truncated => f.write_str(
                "the LLM response was truncated (stopReason \"max_tokens\") and was not \
                 parsed as a plan — retry with a shorter request, or raise the server's \
                 LLM_MAX_TOKENS",
            ),
            ChatError::Refusal(text) => {
                if text.trim().is_empty() {
                    f.write_str("the LLM refused to answer (stopReason \"refusal\")")
                } else {
                    write!(f, "the LLM refused to answer: {}", text.trim())
                }
            }
            ChatError::BadReply(detail) => write!(f, "unexpected LLM response: {detail}"),
            // The reason once (§6.3): the server's own message when it has one.
            ChatError::Disabled(reason) if reason.is_empty() => {
                f.write_str("LLM support is disabled on this server (no API key configured)")
            }
            ChatError::Disabled(reason) => f.write_str(reason),
        }
    }
}

impl std::error::Error for ChatError {}
