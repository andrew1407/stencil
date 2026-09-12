//! §6.3 — a Stencil collaboration server proxying Anthropic (`POST {serverUrl}/llm/chat`).

use serde::Serialize;
use serde_json::Value;

use super::{bearer, to_json, ChatError, ChatMessage, LlmConfig, ProviderMapping, Request};
use crate::llmtransport::{clip, SNIPPET_LEN};

/// §6.3 request body: `protocol.LlmChatRequest` (`model` omitted when empty).
#[derive(Serialize)]
struct ServerBody<'a> {
    system: &'a str,
    messages: Vec<ServerMessage<'a>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    model: Option<&'a str>,
}

#[derive(Serialize)]
struct ServerMessage<'a> {
    role: &'a str,
    text: &'a str,
    #[serde(skip_serializing_if = "Option::is_none")]
    images: Option<Vec<ServerImage<'a>>>,
}

#[derive(Serialize)]
struct ServerImage<'a> {
    #[serde(rename = "mediaType")]
    media_type: &'a str,
    data: &'a str,
}

pub(super) struct StencilServer;

impl ProviderMapping for StencilServer {
    fn request(&self, config: &LlmConfig, system: &str, messages: &[ChatMessage]) -> Request {
        let body = ServerBody {
            system,
            messages: messages
                .iter()
                .map(|m| ServerMessage {
                    role: m.role.as_str(),
                    text: &m.text,
                    images: (!m.images.is_empty()).then(|| {
                        m.images
                            .iter()
                            .map(|i| ServerImage {
                                media_type: &i.media_type,
                                data: &i.data,
                            })
                            .collect()
                    }),
                })
                .collect(),
            model: (!config.model.is_empty()).then_some(config.model.as_str()),
        };
        (
            format!("{}/llm/chat", config.server_url),
            bearer(&config.server_token),
            to_json(&body),
        )
    }

    /// protocol.LlmChatResponse: `text` + `stopReason` (max_tokens/refusal are typed).
    fn reply(&self, value: &Value, raw: &str) -> Result<String, ChatError> {
        let text = value["text"].as_str();
        match value["stopReason"].as_str().unwrap_or("end_turn") {
            "max_tokens" => Err(ChatError::Truncated),
            "refusal" => Err(ChatError::Refusal(text.unwrap_or_default().to_string())),
            _ => text.map(str::to_string).ok_or_else(|| {
                ChatError::BadReply(format!(
                    "no text string in the stencil-server response: {}",
                    clip(raw, SNIPPET_LEN)
                ))
            }),
        }
    }
}
