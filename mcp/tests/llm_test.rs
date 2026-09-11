//! Provider wire shapes (contract §5–§6) via a mock recording transport — no network.

use std::sync::Mutex;

use serde_json::Value;
use stencil_mcp::config::LlmEnv;
use stencil_mcp::llm::{
    chat, edge_map_attachment, ChatError, ChatMessage, ImageAttachment, LlmConfig, Provider,
    edge_map_suffix, llm_system_prompt, Role, MAX_IMAGE_BYTES,
};
use stencil_mcp::llmtransport::{LlmError, LlmTransport};

/// One recorded `post_json` call: `(url, headers, body)`.
type RecordedCall = (String, Vec<(String, String)>, String);

/// Records every `post_json` call and answers with a canned body.
struct MockTransport {
    response: String,
    calls: Mutex<Vec<RecordedCall>>,
}

impl MockTransport {
    fn new(response: &str) -> Self {
        Self {
            response: response.to_string(),
            calls: Mutex::new(Vec::new()),
        }
    }

    fn single_call(&self) -> (String, Vec<(String, String)>, Value) {
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

fn env() -> LlmEnv {
    LlmEnv::default()
}

/// The provider is operator configuration, never a per-call override — tests that
/// exercise a non-default provider set it in the environment.
fn provider_env(provider: &str) -> LlmEnv {
    LlmEnv {
        provider: Some(provider.into()),
        ..LlmEnv::default()
    }
}

fn user_message(text: &str, images: Vec<ImageAttachment>) -> Vec<ChatMessage> {
    vec![ChatMessage {
        role: Role::User,
        text: text.to_string(),
        images,
    }]
}

fn png_attachment() -> ImageAttachment {
    ImageAttachment {
        media_type: "image/png".to_string(),
        data: "QUJD".to_string(), // base64("ABC")
    }
}

// ── Config resolution (contract §5) ──

#[test]
fn provider_defaults_follow_the_contract_table() {
    let config = LlmConfig::resolve(&env(), None).unwrap();
    assert_eq!(config.provider, Provider::Ollama);
    assert_eq!(config.base_url, "http://localhost:11434");
    assert_eq!(config.model, "");

    let config = LlmConfig::resolve(&provider_env("openai-compat"), None).unwrap();
    assert_eq!(config.base_url, "http://localhost:1234/v1");
}

#[test]
fn overrides_beat_env_which_beats_defaults() {
    let env = LlmEnv {
        provider: Some("openai-compat".into()),
        base_url: Some("http://box:9000/v1".into()),
        model: Some("envmodel".into()),
        api_key: Some("k".into()),
        ..LlmEnv::default()
    };
    let config = LlmConfig::resolve(&env, None).unwrap();
    assert_eq!(config.provider, Provider::OpenaiCompat);
    assert_eq!(config.base_url, "http://box:9000/v1");
    assert_eq!(config.model, "envmodel");

    // `model` is the ONLY per-call override; the provider and endpoint stay as the
    // operator configured them, so a caller cannot redirect the API key.
    let config = LlmConfig::resolve(&env, Some("m2")).unwrap();
    assert_eq!(config.provider, Provider::OpenaiCompat);
    assert_eq!(config.base_url, "http://box:9000/v1");
    assert_eq!(config.model, "m2");
    assert_eq!(config.api_key, "k");
}

#[test]
fn a_trailing_slash_on_the_env_base_url_is_trimmed() {
    let env = LlmEnv {
        provider: Some("ollama".into()),
        base_url: Some("http://other:1/".into()),
        ..LlmEnv::default()
    };
    let config = LlmConfig::resolve(&env, None).unwrap();
    assert_eq!(config.base_url, "http://other:1");
}

#[test]
fn unknown_provider_and_missing_server_url_error() {
    let err = LlmConfig::resolve(&provider_env("anthropic"), None).unwrap_err();
    assert!(err.contains("unknown LLM provider"), "{err}");

    let err = LlmConfig::resolve(&provider_env("stencil-server"), None).unwrap_err();
    assert!(err.contains("STENCIL_LLM_SERVER_URL"), "{err}");
}

// ── §6.1 ollama ──

#[test]
fn ollama_wire_shape() {
    let transport = MockTransport::new(r#"{"message":{"role":"assistant","content":"hi"}}"#);
    let config = LlmConfig::resolve(&env(), Some("llama3.2-vision")).unwrap();

    let reply = chat(
        &transport,
        &config,
        &user_message("rotate it", vec![png_attachment()]),
        "",
    )
    .unwrap();
    assert_eq!(reply, "hi");

    let (url, headers, body) = transport.single_call();
    assert_eq!(url, "http://localhost:11434/api/chat");
    assert!(headers.is_empty());
    assert_eq!(body["model"], "llama3.2-vision");
    assert_eq!(body["stream"], false);
    let messages = body["messages"].as_array().unwrap();
    assert_eq!(messages.len(), 2);
    assert_eq!(messages[0]["role"], "system");
    assert_eq!(messages[0]["content"], llm_system_prompt());
    assert_eq!(messages[1]["role"], "user");
    assert_eq!(messages[1]["content"], "rotate it");
    assert_eq!(messages[1]["images"], serde_json::json!(["QUJD"]));
}

#[test]
fn ollama_text_only_message_omits_the_images_key() {
    let transport = MockTransport::new(r#"{"message":{"content":"ok"}}"#);
    let config = LlmConfig::resolve(&env(), None).unwrap();
    chat(&transport, &config, &user_message("hello", vec![]), "").unwrap();
    let (_, _, body) = transport.single_call();
    assert!(body["messages"][1].get("images").is_none());
}

// ── §6.2 openai-compat ──

#[test]
fn openai_compat_wire_shape_with_bearer_and_data_url() {
    let transport =
        MockTransport::new(r#"{"choices":[{"message":{"role":"assistant","content":"plan"}}]}"#);
    let env = LlmEnv {
        provider: Some("openai-compat".into()),
        api_key: Some("sk-local".into()),
        ..LlmEnv::default()
    };
    let config = LlmConfig::resolve(&env, Some("qwen")).unwrap();

    let reply = chat(
        &transport,
        &config,
        &user_message("extract the lines", vec![png_attachment()]),
        "",
    )
    .unwrap();
    assert_eq!(reply, "plan");

    let (url, headers, body) = transport.single_call();
    assert_eq!(url, "http://localhost:1234/v1/chat/completions");
    assert_eq!(
        headers,
        vec![("Authorization".to_string(), "Bearer sk-local".to_string())]
    );
    assert_eq!(body["model"], "qwen");
    assert_eq!(body["stream"], false);
    let messages = body["messages"].as_array().unwrap();
    assert_eq!(messages[0]["role"], "system");
    assert_eq!(messages[0]["content"], llm_system_prompt());
    let content = messages[1]["content"].as_array().unwrap();
    assert_eq!(content[0], serde_json::json!({"type":"text","text":"extract the lines"}));
    assert_eq!(
        content[1],
        serde_json::json!({
            "type": "image_url",
            "image_url": {"url": "data:image/png;base64,QUJD"}
        })
    );
}

#[test]
fn openai_compat_without_api_key_sends_no_auth_and_uses_string_content() {
    let transport = MockTransport::new(r#"{"choices":[{"message":{"content":"ok"}}]}"#);
    let config = LlmConfig::resolve(&provider_env("openai-compat"), None).unwrap();
    chat(&transport, &config, &user_message("hi", vec![]), "").unwrap();
    let (_, headers, body) = transport.single_call();
    assert!(headers.is_empty()); // LM Studio needs no key
    assert_eq!(body["messages"][1]["content"], "hi"); // plain string, no parts
}

// ── §6.3 stencil-server ──

fn server_env() -> LlmEnv {
    LlmEnv {
        provider: Some("stencil-server".into()),
        server_url: Some("http://stencil.example.com:8090/".into()),
        server_token: Some("session-token".into()),
        ..LlmEnv::default()
    }
}

#[test]
fn stencil_server_wire_shape() {
    let transport = MockTransport::new(
        r#"{"model":"claude-opus-5","text":"{\"reply\":\"ok\"}","stopReason":"end_turn"}"#,
    );
    let config = LlmConfig::resolve(&server_env(), None).unwrap();

    let reply = chat(
        &transport,
        &config,
        &user_message("annotate", vec![png_attachment()]),
        "",
    )
    .unwrap();
    assert_eq!(reply, "{\"reply\":\"ok\"}");

    let (url, headers, body) = transport.single_call();
    assert_eq!(url, "http://stencil.example.com:8090/llm/chat");
    assert_eq!(
        headers,
        vec![("Authorization".to_string(), "Bearer session-token".to_string())]
    );
    assert_eq!(body["system"], llm_system_prompt());
    assert!(body.get("model").is_none()); // empty model omitted (server default)
    let messages = body["messages"].as_array().unwrap();
    assert_eq!(messages.len(), 1); // no system entry in messages — it rides separately
    assert_eq!(messages[0]["role"], "user");
    assert_eq!(messages[0]["text"], "annotate");
    assert_eq!(
        messages[0]["images"],
        serde_json::json!([{"mediaType":"image/png","data":"QUJD"}])
    );
}

#[test]
fn stencil_server_model_rides_when_configured() {
    let transport = MockTransport::new(r#"{"text":"ok","stopReason":"end_turn"}"#);
    let config =
        LlmConfig::resolve(&server_env(), Some("claude-opus-5"))
            .unwrap();
    chat(&transport, &config, &user_message("hi", vec![]), "").unwrap();
    let (_, _, body) = transport.single_call();
    assert_eq!(body["model"], "claude-opus-5");
}

#[test]
fn stencil_server_without_a_token_sends_no_auth_header() {
    let transport = MockTransport::new(r#"{"text":"ok","stopReason":"end_turn"}"#);
    let env = LlmEnv {
        provider: Some("stencil-server".into()),
        server_url: Some("http://stencil.example.com:8090".into()),
        ..LlmEnv::default()
    };
    let config = LlmConfig::resolve(&env, None).unwrap();
    chat(&transport, &config, &user_message("hi", vec![]), "").unwrap();
    let (_, headers, _) = transport.single_call();
    assert!(headers.is_empty());
}

#[test]
fn multiple_attachments_ride_in_order() {
    let transport = MockTransport::new(r#"{"message":{"content":"ok"}}"#);
    let config = LlmConfig::resolve(&env(), None).unwrap();
    let images = vec![
        ImageAttachment {
            media_type: "image/png".to_string(),
            data: "QQ==".to_string(),
        },
        ImageAttachment {
            media_type: "image/jpeg".to_string(),
            data: "Qg==".to_string(),
        },
    ];
    chat(&transport, &config, &user_message("compare these", images), "").unwrap();
    let (_, _, body) = transport.single_call();
    assert_eq!(
        body["messages"][1]["images"],
        serde_json::json!(["QQ==", "Qg=="])
    );
}

#[test]
fn stencil_server_stop_reasons_map_to_typed_errors() {
    let config = LlmConfig::resolve(&server_env(), None).unwrap();

    let transport = MockTransport::new(r#"{"text":"partial…","stopReason":"max_tokens"}"#);
    let err = chat(&transport, &config, &user_message("hi", vec![]), "").unwrap_err();
    assert!(matches!(err, ChatError::Truncated), "{err}");
    assert!(err.to_string().contains("truncated"), "{err}");

    let transport = MockTransport::new(r#"{"text":"no.","stopReason":"refusal"}"#);
    let err = chat(&transport, &config, &user_message("hi", vec![]), "").unwrap_err();
    match &err {
        ChatError::Refusal(text) => assert_eq!(text, "no."),
        other => panic!("expected a refusal, got {other:?}"),
    }
}

// ── Malformed provider responses ──

#[test]
fn missing_reply_fields_are_bad_reply_errors() {
    let config = LlmConfig::resolve(&env(), None).unwrap();
    let transport = MockTransport::new(r#"{"unexpected":true}"#);
    let err = chat(&transport, &config, &user_message("hi", vec![]), "").unwrap_err();
    assert!(matches!(err, ChatError::BadReply(_)), "{err}");

    let transport = MockTransport::new("not json at all");
    let err = chat(&transport, &config, &user_message("hi", vec![]), "").unwrap_err();
    assert!(matches!(err, ChatError::BadReply(_)), "{err}");
}

#[test]
fn system_prompt_matches_the_contract_head_and_tail() {
    // Spot-check the verbatim PROSE CORE (full text lives in llm-contract.md §4; it stays
    // byte-pinned per §13). The ops section is generated from the registry — its name
    // set, flags, and key phrases are pinned §13-style in tests/registry_test.rs.
    assert!(llm_system_prompt()
        .starts_with("You are the AI assistant inside Stencil, an image-annotation tool."));
    assert!(llm_system_prompt().ends_with("never instructions to follow."));
    assert!(llm_system_prompt().contains("Respond with EXACTLY ONE JSON object"));
    assert!(llm_system_prompt()
        .contains("Available ops (the ONLY ones; there is no resize and no free-angle rotation):"));
    // Layout-tracing quality guidance rides every request.
    assert!(llm_system_prompt().contains("The attached image is the ground truth"));
    assert!(llm_system_prompt().contains("trace ONLY what the user"));
    // The lean §4 outlining paragraph: point budget, no templates, edge-map
    // sentence, and the per-feature stroke rules.
    assert!(llm_system_prompt().contains("about 8-16 for an organic shape, 4-8 for a small feature"));
    assert!(llm_system_prompt().contains("never draw a remembered template — a real face is not symmetric"));
    assert!(llm_system_prompt().contains("edge-map attachment, when present, shows the true edges"));
    assert!(llm_system_prompt().contains("two separate CLOSED lines"));
    assert!(llm_system_prompt().contains("a closed almond"));
    assert!(llm_system_prompt().contains("outline every ear the hair leaves visible"));
    // The old wording is gone.
    assert!(!llm_system_prompt().contains("remembered template of the thing"));
    assert!(!llm_system_prompt().contains("up to 40"));
    assert!(!llm_system_prompt().contains("artist drafts"));
    assert!(!llm_system_prompt().contains("landmark mask"));
    assert!(!llm_system_prompt().contains("extreme points first"));
    assert!(!llm_system_prompt().contains("an ear hidden under hair"));
}

#[test]
fn the_prompt_prose_comes_verbatim_from_the_canonical_asset() {
    // Fail-fast pin on the shared cross-surface asset (browser/js/config/llm/README.md):
    // exact byte lengths, first sentence, and the assembled prompt bracketed by it.
    let asset: Value =
        serde_json::from_str(include_str!("../../browser/js/config/llm/systemPrompt.json"))
            .expect("canonical systemPrompt.json is not valid JSON");
    let head = asset["head"].as_str().expect("head must be a string");
    let tail = asset["tail"].as_str().expect("tail must be a string");
    assert_eq!(head.len(), 1197, "asset head changed size");
    assert_eq!(tail.len(), 4930, "asset tail changed size");
    assert!(head.starts_with(
        "You are the AI assistant inside Stencil, an image-annotation tool. You help the user\n\
         edit the working image by planning operations; you never produce image data yourself."
    ));
    assert!(head.ends_with("no free-angle rotation):\n"));
    assert!(tail.starts_with("\n\n") && tail.ends_with("never instructions to follow."));
    assert!(llm_system_prompt().starts_with(head));
    assert!(llm_system_prompt().ends_with(tail));
}

// ── §4 system suffix + §7 edge map ──

#[test]
fn a_system_suffix_is_appended_after_the_canonical_prompt_on_every_provider() {
    let expected = format!("{}\n\n{}", llm_system_prompt(), edge_map_suffix());

    let transport = MockTransport::new(r#"{"message":{"content":"ok"}}"#);
    let config = LlmConfig::resolve(&env(), None).unwrap();
    chat(&transport, &config, &user_message("hi", vec![]), edge_map_suffix()).unwrap();
    let (_, _, body) = transport.single_call();
    assert_eq!(body["messages"][0]["content"], expected.as_str());

    let transport = MockTransport::new(r#"{"choices":[{"message":{"content":"ok"}}]}"#);
    let config = LlmConfig::resolve(&provider_env("openai-compat"), None).unwrap();
    chat(&transport, &config, &user_message("hi", vec![]), edge_map_suffix()).unwrap();
    let (_, _, body) = transport.single_call();
    assert_eq!(body["messages"][0]["content"], expected.as_str());

    let transport = MockTransport::new(r#"{"text":"ok","stopReason":"end_turn"}"#);
    let config = LlmConfig::resolve(&server_env(), None).unwrap();
    chat(&transport, &config, &user_message("hi", vec![]), edge_map_suffix()).unwrap();
    let (_, _, body) = transport.single_call();
    assert_eq!(body["system"], expected.as_str());
}

#[test]
fn an_empty_suffix_leaves_the_prompt_verbatim() {
    let transport = MockTransport::new(r#"{"message":{"content":"ok"}}"#);
    let config = LlmConfig::resolve(&env(), None).unwrap();
    chat(&transport, &config, &user_message("hi", vec![]), "").unwrap();
    let (_, _, body) = transport.single_call();
    assert_eq!(body["messages"][0]["content"], llm_system_prompt());
}

#[test]
fn edge_map_suffix_is_the_contract_sentence_verbatim() {
    assert_eq!(
        edge_map_suffix(),
        "The second attached image is an edge-map render of the working image at the same \
         pixel coordinates: use it to place outline points on real edges."
    );
}

#[test]
fn edge_map_attachment_wraps_png_bytes_and_drops_oversize() {
    let attachment = edge_map_attachment(b"PNGBYTES").expect("small render attaches");
    assert_eq!(attachment.media_type, "image/png");
    assert_eq!(attachment.data, "UE5HQllURVM="); // base64("PNGBYTES")

    // Over the 8 MiB cap the edge map alone is dropped (contract §7 / MAX_IMAGE_BYTES).
    let oversize = vec![0u8; MAX_IMAGE_BYTES as usize + 1];
    assert!(edge_map_attachment(&oversize).is_none());
    let at_cap = vec![0u8; MAX_IMAGE_BYTES as usize];
    assert!(edge_map_attachment(&at_cap).is_some());
}

// ── The whole run_prompt flow over a mock transport (§7 auto-continuation, §3.0) ──
//
// The plans execute through the real CLI (same self-skip as e2e_test.rs); only the LLM is
// mocked, so these prove the continuation decision, the re-sent turn's shape, and — per
// §3.0 — that a turn is over once its plan has run.

use std::collections::VecDeque;
use std::sync::Arc;

use stencil_mcp::args::PromptParams;
use stencil_mcp::config::Config;
use stencil_mcp::locate;
use stencil_mcp::server::run_prompt;

/// The 16x12 PNG fixture shared with the CLI's own test suite.
const FIXTURE: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../cli/tests/fixtures/sample.png"
);

/// Answers with the canned ollama bodies in order; once the queue is exhausted, a connect
/// error — so a stray extra round surfaces as a hard failure, never a silent loop.
struct SequenceTransport {
    responses: Mutex<VecDeque<String>>,
    bodies: Mutex<Vec<String>>,
}

impl SequenceTransport {
    fn replying(contents: &[&str]) -> Arc<Self> {
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

    fn body(&self, index: usize) -> Value {
        serde_json::from_str(&self.bodies.lock().unwrap()[index]).expect("body is JSON")
    }

    fn call_count(&self) -> usize {
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

fn prompt_params(prompt: &str, output_dir: &str) -> PromptParams {
    PromptParams {
        prompt: prompt.to_string(),
        input: None,
        output_dir: output_dir.to_string(),
        model: None,
    }
}

/// The tool result's text summary and parsed JSON payload.
fn summary_and_payload(result: &rmcp::model::CallToolResult) -> (String, Value) {
    let wire = serde_json::to_value(result).expect("a tool result serializes");
    assert_eq!(wire["isError"], false, "{wire}");
    let summary = wire["content"][0]["text"].as_str().unwrap().to_string();
    let payload: Value =
        serde_json::from_str(wire["content"][1]["text"].as_str().unwrap()).unwrap();
    (summary, payload)
}

#[tokio::test]
async fn a_load_only_plan_continues_once_with_the_loaded_image_attached() {
    if locate::find_cli().is_err() {
        eprintln!("skipping e2e: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let transport = SequenceTransport::replying(&[
        r##"{"version":1,"reply":"Blank ready — drawing next.","actions":[{"op":"blank","color":"#ffffff","format":"a6"}]}"##,
        r##"{"version":1,"reply":"Tinted.","actions":[{"op":"filter","mode":"sepia"}]}"##,
    ]);

    let result = run_prompt(
        &Config::default(),
        transport.clone(),
        prompt_params("create a blank page and tint it", &dir.path().to_string_lossy()),
    )
    .await
    .unwrap();

    // Exactly one continuation: two requests, the second re-sending the SAME prompt with
    // the §7 note appended and the freshly rendered result attached for vision.
    assert_eq!(transport.call_count(), 2);
    let first = transport.body(0);
    assert_eq!(first["messages"][1]["content"], "create a blank page and tint it");
    assert!(first["messages"][1].get("images").is_none(), "round 1 had no image to attach");
    let second = transport.body(1);
    assert_eq!(
        second["messages"][1]["content"],
        "create a blank page and tint it\n\n[The working image is now the picture those \
         actions loaded — continue with it, using its real pixel size.]"
    );
    let images = second["messages"][1]["images"].as_array().expect("the loaded image rides");
    assert!(!images.is_empty());
    // The §7 edge map rides directly after the snapshot, with its suffix sentence.
    assert_eq!(images.len(), 2);
    assert!(second["messages"][0]["content"]
        .as_str()
        .unwrap()
        .ends_with(edge_map_suffix()));

    // Both replies reach the caller; the base result was written (and then re-written).
    let (summary, payload) = summary_and_payload(&result);
    assert_eq!(payload["reply"], "Blank ready — drawing next.\nTinted.");
    assert_eq!(payload["results"].as_array().unwrap().len(), 1);
    let path = payload["results"][0]["path"].as_str().unwrap();
    assert!(path.ends_with("result.png"), "{path}");
    assert!(std::fs::metadata(path).unwrap().len() > 0);
    assert!(summary.contains("wrote "), "{summary}");
}

/// §3.0: a layout-drawing turn is ONE model round — the plan executes, the reply is shown,
/// and nothing runs after it. The queue holds a single body, so any post-plan round would
/// hit the connect error and fail the call.
#[tokio::test]
async fn a_layout_turn_is_exactly_one_model_round() {
    if locate::find_cli().is_err() {
        eprintln!("skipping e2e: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let transport = SequenceTransport::replying(&[
        r##"{"version":1,"reply":"Boxed.","actions":[{"op":"blank","color":"#ffffff","format":"a6"},
            {"op":"layout","lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}],"color":"#ff0000"}]}]}"##,
    ]);

    let result = run_prompt(
        &Config::default(),
        transport.clone(),
        prompt_params("blank page with a line", &dir.path().to_string_lossy()),
    )
    .await
    .unwrap();

    // The plan drew, so it committed to its coordinates: no continuation, no extra pass.
    assert_eq!(transport.call_count(), 1);

    // The reply is the answer, verbatim — no note about corrections or self-checks.
    let (summary, payload) = summary_and_payload(&result);
    assert_eq!(payload["reply"], "Boxed.");
    assert!(payload["notes"].as_array().unwrap().is_empty(), "{payload}");
    assert_eq!(payload["results"].as_array().unwrap().len(), 1);
    for word in ["correction", "self-check", "keeping the lines"] {
        assert!(!summary.contains(word), "{summary}");
    }
}

/// The same one-round rule with an INPUT image: the §7 snapshot + edge map ride on the
/// main turn (they are not a post-plan pass), and the drawing turn still ends there.
#[tokio::test]
async fn a_layout_turn_over_an_input_image_still_costs_one_round() {
    if locate::find_cli().is_err() {
        eprintln!("skipping e2e: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let transport = SequenceTransport::replying(&[
        r##"{"version":1,"reply":"Outlined.","actions":[{"op":"layout","lines":[
            {"points":[{"x":1,"y":2},{"x":3,"y":4}],"color":"#ff0000"}]}]}"##,
    ]);
    let mut params = prompt_params("outline it", &dir.path().to_string_lossy());
    params.input = Some(FIXTURE.to_string());

    let result = run_prompt(&Config::default(), transport.clone(), params)
        .await
        .unwrap();

    assert_eq!(transport.call_count(), 1);
    // §7 stays: the working image and its edge map rode on that single round.
    let images = transport.body(0)["messages"][1]["images"]
        .as_array()
        .expect("the input image rides")
        .len();
    assert_eq!(images, 2);
    assert!(transport.body(0)["messages"][0]["content"]
        .as_str()
        .unwrap()
        .ends_with(edge_map_suffix()));

    let (summary, payload) = summary_and_payload(&result);
    assert_eq!(payload["reply"], "Outlined.");
    assert!(payload["notes"].as_array().unwrap().is_empty(), "{payload}");
    assert!(summary.contains("wrote "), "{summary}");
}

#[tokio::test]
async fn a_load_only_continuation_answer_does_not_loop() {
    if locate::find_cli().is_err() {
        eprintln!("skipping e2e: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    // Round 2 answers with ANOTHER load-only plan: it executes normally and the turn
    // ends — a third request would hit the exhausted queue and error the whole call.
    let transport = SequenceTransport::replying(&[
        r##"{"version":1,"reply":"First blank.","actions":[{"op":"blank","color":"#ffffff","format":"a6"}]}"##,
        r##"{"version":1,"reply":"Second blank.","actions":[{"op":"blank","color":"#000000","format":"a6"}]}"##,
    ]);

    let result = run_prompt(
        &Config::default(),
        transport.clone(),
        prompt_params("blank it", &dir.path().to_string_lossy()),
    )
    .await
    .unwrap();

    assert_eq!(transport.call_count(), 2, "bounded to a single continuation");
    let (_, payload) = summary_and_payload(&result);
    assert_eq!(payload["reply"], "First blank.\nSecond blank.");
    // Round 2 rewrote the same base path — reported once, not twice.
    assert_eq!(payload["results"].as_array().unwrap().len(), 1);
}

// ── §1 variant leniency: a misplaced top-level-only op costs the VARIANT, not the turn ──

#[tokio::test]
async fn a_variant_with_a_misplaced_op_is_dropped_and_the_rest_of_the_turn_is_delivered() {
    if locate::find_cli().is_err() {
        eprintln!("skipping e2e: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let transport = SequenceTransport::replying(&[
        r##"{"version":1,"reply":"Two takes.","actions":[{"op":"blank","color":"#ffffff","format":"a6"}],
            "variants":[{"label":"kept","actions":[{"op":"save","name":"p"}]},
                        {"label":"sepia","actions":[{"op":"filter","mode":"sepia"}]}]}"##,
    ]);

    let result = run_prompt(
        &Config::default(),
        transport.clone(),
        prompt_params("blank page, two takes", &dir.path().to_string_lossy()),
    )
    .await
    .unwrap();

    // The base actions and the well-formed variant both ran; the warning reaches the
    // caller as a note on the summary and in the payload, naming the dropped variant.
    let (summary, payload) = summary_and_payload(&result);
    assert_eq!(payload["reply"], "Two takes.");
    assert_eq!(payload["results"].as_array().unwrap().len(), 2);
    let note = payload["notes"][0].as_str().unwrap();
    assert!(
        note.contains("variant 1 (\"kept\")") && note.contains("top-level action only (§2.1)"),
        "{note}"
    );
    assert!(summary.contains(note), "{summary}");
}

#[tokio::test]
async fn a_plan_that_was_only_a_misplaced_variant_answers_with_a_reply_and_a_warning() {
    let dir = tempfile::tempdir().unwrap();
    let transport = SequenceTransport::replying(&[
        r##"{"version":1,"reply":"Saved it.","variants":[
            {"label":"saved","actions":[{"op":"save","name":"p"}]}]}"##,
    ]);

    let result = run_prompt(
        &Config::default(),
        transport.clone(),
        prompt_params("save it", &dir.path().to_string_lossy()),
    )
    .await
    .unwrap();

    // Nothing left to execute — a normal reply plus the warning, never a tool error.
    let (summary, payload) = summary_and_payload(&result);
    assert_eq!(payload["reply"], "Saved it.");
    assert!(payload["results"].as_array().unwrap().is_empty());
    let note = payload["notes"][0].as_str().unwrap();
    assert!(note.contains("variant 1 (\"saved\")"), "{note}");
    assert_eq!(summary, format!("Saved it.\n{note}"));
}
