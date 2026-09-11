//! §6.1 — native Ollama chat (`POST {baseUrl}/api/chat`).

use serde::Serialize;
use serde_json::Value;

use super::{to_json, ChatBody, ChatError, ChatMessage, LlmConfig, ProviderMapping, Request};
use crate::llmtransport::{clip, SNIPPET_LEN};

#[derive(Serialize)]
struct OllamaMessage<'a> {
    role: &'a str,
    content: &'a str,
    #[serde(skip_serializing_if = "Option::is_none")]
    images: Option<Vec<&'a str>>,
}

pub(super) struct Ollama;

impl ProviderMapping for Ollama {
    fn request(&self, config: &LlmConfig, system: &str, messages: &[ChatMessage]) -> Request {
        let mut wire = vec![OllamaMessage {
            role: "system",
            content: system,
            images: None,
        }];
        wire.extend(messages.iter().map(|m| OllamaMessage {
            role: m.role.as_str(),
            content: &m.text,
            images: (!m.images.is_empty())
                .then(|| m.images.iter().map(|i| i.data.as_str()).collect()),
        }));
        let body = ChatBody {
            model: &config.model,
            stream: false,
            messages: wire,
        };
        (
            format!("{}/api/chat", config.base_url),
            Vec::new(),
            to_json(&body),
        )
    }

    /// Reply text = `message.content`.
    fn reply(&self, value: &Value, raw: &str) -> Result<String, ChatError> {
        value["message"]["content"]
            .as_str()
            .map(str::to_string)
            .ok_or_else(|| {
                ChatError::BadReply(format!(
                    "no message.content string in the ollama response: {}",
                    clip(raw, SNIPPET_LEN)
                ))
            })
    }
}
