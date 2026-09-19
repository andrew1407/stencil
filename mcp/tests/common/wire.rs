//! The shared LLM wire fixtures' plumbing: corpus reading, the capturing transport and the case builders.
use std::sync::Mutex;

use serde_json::Value;
use stencil_mcp::llm::{ChatMessage, ImageAttachment, LlmConfig, Provider, Role};
use stencil_mcp::llmtransport::{error_code, error_reason, LlmError, LlmTransport};

pub fn load_array(path: &str) -> Vec<Value> {
    let raw = std::fs::read_to_string(path).unwrap_or_else(|e| panic!("cannot read {path}: {e}"));
    // One browser `expect` ends in a lone high surrogate (`\ud83d`), which serde_json refuses;
    // that case's expectation is recomputed for mcp via an override.
    let raw = raw.replace("\\ud83d", "\\ufffd");
    serde_json::from_str(&raw).unwrap_or_else(|e| panic!("{path} is not a JSON array: {e}"))
}

// ── providerWire ──

pub struct Recorded {
    pub url: String,
    pub headers: Vec<(String, String)>,
    pub body: String,
}

/// Capturing transport: records the request, answers with the case's canned response.
/// Non-2xx bodies go through the SAME `error_reason` seam `PlainHttpTransport` uses.
pub struct MockTransport {
    pub canned: Result<String, (u16, String)>,
    pub seen: Mutex<Option<Recorded>>,
}

impl LlmTransport for MockTransport {
    fn post_json(
        &self,
        url: &str,
        headers: &[(String, String)],
        body: &str,
    ) -> Result<String, LlmError> {
        *self.seen.lock().unwrap() = Some(Recorded {
            url: url.to_string(),
            headers: headers.to_vec(),
            body: body.to_string(),
        });
        match &self.canned {
            Ok(text) => Ok(text.clone()),
            Err((status, body)) => Err(LlmError::Status {
                status: *status,
                reason: error_reason(body),
                code: error_code(body),
            }),
        }
    }
}

pub fn config_for(case: &Value) -> LlmConfig {
    let provider = match case["provider"].as_str().unwrap() {
        "ollama" => Provider::Ollama,
        "openai" => Provider::OpenaiCompat,
        "server" => Provider::StencilServer,
        other => panic!("unknown fixture provider {other}"),
    };
    let s = &case["settings"];
    let get = |v: &Value| v.as_str().unwrap_or_default().trim_end_matches('/').to_string();
    LlmConfig {
        provider,
        base_url: get(&s["baseUrl"]),
        model: s["model"].as_str().unwrap_or_default().to_string(),
        api_key: s["apiKey"].as_str().unwrap_or_default().to_string(),
        server_url: get(&s["serverUrl"]),
        server_token: case["token"].as_str().unwrap_or_default().to_string(),
    }
}

pub fn messages_for(case: &Value) -> Vec<ChatMessage> {
    case["chat"]["messages"]
        .as_array()
        .expect("chat.messages")
        .iter()
        .map(|m| ChatMessage {
            role: match m["role"].as_str().unwrap() {
                "user" => Role::User,
                "assistant" => Role::Assistant,
                other => panic!("unknown role {other}"),
            },
            text: m["text"].as_str().unwrap_or_default().to_string(),
            images: m["images"]
                .as_array()
                .map(|images| {
                    images
                        .iter()
                        .map(|i| ImageAttachment {
                            media_type: i["mediaType"].as_str().unwrap().to_string(),
                            data: i["data"].as_str().unwrap().to_string(),
                        })
                        .collect()
                })
                .unwrap_or_default(),
        })
        .collect()
}

/// The mutable slot the system text occupies in a request body, per provider.
pub fn system_slot<'a>(provider: Provider, body: &'a mut Value) -> &'a mut Value {
    match provider {
        Provider::Ollama | Provider::OpenaiCompat => &mut body["messages"][0]["content"],
        Provider::StencilServer => &mut body["system"],
    }
}
