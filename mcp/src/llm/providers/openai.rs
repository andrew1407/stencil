//! §6.2 — OpenAI-compatible servers (LM Studio, llama.cpp, vLLM …,
//! `POST {baseUrl}/chat/completions`): images as data-URL content parts, optional bearer key.

use serde::Serialize;
use serde_json::Value;

use super::{bearer, to_json, ChatBody, ChatError, ChatMessage, LlmConfig, ProviderMapping, Request};
use crate::llmtransport::{clip, SNIPPET_LEN};

#[derive(Serialize)]
struct OpenaiMessage<'a> {
    role: &'a str,
    content: OpenaiContent<'a>,
}

#[derive(Serialize)]
#[serde(untagged)]
enum OpenaiContent<'a> {
    Text(&'a str),
    Parts(Vec<OpenaiPart<'a>>),
}

#[derive(Serialize)]
#[serde(tag = "type")]
enum OpenaiPart<'a> {
    #[serde(rename = "text")]
    Text { text: &'a str },
    #[serde(rename = "image_url")]
    ImageUrl { image_url: DataUrl },
}

#[derive(Serialize)]
struct DataUrl {
    url: String,
}

pub(super) struct OpenaiCompat;

impl ProviderMapping for OpenaiCompat {
    fn request(&self, config: &LlmConfig, system: &str, messages: &[ChatMessage]) -> Request {
        let mut wire = vec![OpenaiMessage {
            role: "system",
            content: OpenaiContent::Text(system),
        }];
        wire.extend(messages.iter().map(|m| OpenaiMessage {
            role: m.role.as_str(),
            content: if m.images.is_empty() {
                OpenaiContent::Text(&m.text)
            } else {
                let mut parts = vec![OpenaiPart::Text { text: &m.text }];
                parts.extend(m.images.iter().map(|i| OpenaiPart::ImageUrl {
                    image_url: DataUrl {
                        url: format!("data:{};base64,{}", i.media_type, i.data),
                    },
                }));
                OpenaiContent::Parts(parts)
            },
        }));
        let body = ChatBody {
            model: &config.model,
            stream: false,
            messages: wire,
        };
        (
            format!("{}/chat/completions", config.base_url),
            bearer(&config.api_key),
            to_json(&body),
        )
    }

    /// Reply text = `choices[0].message.content`.
    fn reply(&self, value: &Value, raw: &str) -> Result<String, ChatError> {
        value["choices"][0]["message"]["content"]
            .as_str()
            .map(str::to_string)
            .ok_or_else(|| {
                ChatError::BadReply(format!(
                    "no choices[0].message.content string in the response: {}",
                    clip(raw, SNIPPET_LEN)
                ))
            })
    }
}
