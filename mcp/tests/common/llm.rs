//! LLM test fixtures: the two recording transports and the env / message / result
//! builders the provider-wire and run_prompt suites share.

use std::collections::VecDeque;
use std::sync::{Arc, Mutex};

use serde_json::Value;
use stencil_mcp::args::PromptParams;
use stencil_mcp::config::LlmEnv;
use stencil_mcp::llm::{ChatMessage, ImageAttachment, Role};
use stencil_mcp::llmtransport::{LlmError, LlmTransport};

/// One recorded `post_json` call: `(url, headers, body)`.
pub type RecordedCall = (String, Vec<(String, String)>, String);

/// Records every `post_json` call and answers with a canned body.
pub struct MockTransport {
    response: String,
    calls: Mutex<Vec<RecordedCall>>,
}

impl MockTransport {
    pub fn new(response: &str) -> Self {
        Self {
            response: response.to_string(),
            calls: Mutex::new(Vec::new()),
        }
    }

    pub fn single_call(&self) -> (String, Vec<(String, String)>, Value) {
        let calls = self.calls.lock().unwrap();
        assert_eq!(calls.len(), 1, "expected exactly one request");
        let (url, headers, body) = calls[0].clone();
        (url, headers, serde_json::from_str(&body).expect("body is JSON"))
    }
}

impl LlmTransport for MockTransport {
    fn post_json(
        &self,
        url: &str,
        headers: &[(String, String)],
        body: &str,
    ) -> Result<String, LlmError> {
        self.calls
            .lock()
            .unwrap()
            .push((url.to_string(), headers.to_vec(), body.to_string()));
        Ok(self.response.clone())
    }
}

pub fn env() -> LlmEnv {
    LlmEnv::default()
}

/// The provider is operator configuration, never a per-call override — tests that
/// exercise a non-default provider set it in the environment.
pub fn provider_env(provider: &str) -> LlmEnv {
    LlmEnv {
        provider: Some(provider.into()),
        ..LlmEnv::default()
    }
}

pub fn user_message(text: &str, images: Vec<ImageAttachment>) -> Vec<ChatMessage> {
    vec![ChatMessage {
        role: Role::User,
        text: text.to_string(),
        images,
    }]
}

pub fn png_attachment() -> ImageAttachment {
    ImageAttachment {
        media_type: "image/png".to_string(),
        data: "QUJD".to_string(), // base64("ABC")
    }
}

pub fn server_env() -> LlmEnv {
    LlmEnv {
        provider: Some("stencil-server".into()),
        server_url: Some("http://stencil.example.com:8090/".into()),
        server_token: Some("session-token".into()),
        ..LlmEnv::default()
    }
}

/// The 16x12 PNG fixture shared with the CLI's own test suite.
pub const FIXTURE: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../cli/tests/fixtures/sample.png"
);

/// Answers with the canned ollama bodies in order; once the queue is exhausted, a connect
/// error — so a stray extra round surfaces as a hard failure, never a silent loop.
pub struct SequenceTransport {
    responses: Mutex<VecDeque<String>>,
    bodies: Mutex<Vec<String>>,
}

impl SequenceTransport {
    pub fn replying(contents: &[&str]) -> Arc<Self> {
        Arc::new(Self {
            responses: Mutex::new(
                contents
                    .iter()
                    .map(|content| {
                        serde_json::json!({"message": {"content": content}}).to_string()
                    })
                    .collect(),
            ),
            bodies: Mutex::new(Vec::new()),
        })
    }

    pub fn body(&self, index: usize) -> Value {
        serde_json::from_str(&self.bodies.lock().unwrap()[index]).expect("body is JSON")
    }

    pub fn call_count(&self) -> usize {
        self.bodies.lock().unwrap().len()
    }
}

impl LlmTransport for SequenceTransport {
    fn post_json(
        &self,
        _url: &str,
        _headers: &[(String, String)],
        body: &str,
    ) -> Result<String, LlmError> {
        self.bodies.lock().unwrap().push(body.to_string());
        match self.responses.lock().unwrap().pop_front() {
            Some(response) => Ok(response),
            None => Err(LlmError::Connect("no route to host".to_string())),
        }
    }
}

pub fn prompt_params(prompt: &str, output_dir: &str) -> PromptParams {
    PromptParams {
        prompt: prompt.to_string(),
        input: None,
        output_dir: output_dir.to_string(),
        model: None,
    }
}

/// The tool result's text summary and parsed JSON payload.
pub fn summary_and_payload(result: &rmcp::model::CallToolResult) -> (String, Value) {
    let wire = serde_json::to_value(result).expect("a tool result serializes");
    assert_eq!(wire["isError"], false, "{wire}");
    let summary = wire["content"][0]["text"].as_str().unwrap().to_string();
    let payload: Value =
        serde_json::from_str(wire["content"][1]["text"].as_str().unwrap()).unwrap();
    (summary, payload)
}
