//! Walk the shared LLM wire fixtures through mcp's real client:
//!
//! - `browser/js/config/llm/fixtures/providerWire/` — build each request via `llm::chat`
//!   against a capturing mock transport, deep-compare the JSON body (absence is contract),
//!   then feed the canned response/error and check the extracted reply / typed error.
//!   mcp hard-codes its own §4 canonical system prompt (the fixtures carry the browser's
//!   short stand-in), so the walker first asserts mcp sent exactly `llm_system_prompt()`
//!   and then substitutes the fixture's system text before the deep compare.
//! - `browser/js/config/llm/fixtures/sanitizer/cases.json` — vectors for
//!   `llmtransport::sanitize_detail`. `expect` is the BROWSER sanitizer's output; where
//!   mcp's word-based sanitizer differs, `tests/fixture_overrides.json` pins mcp's own
//!   output (schema: divergent walkers recompute, keeping the no-URL/length invariants).
//!
//! This phase PINS current behavior — overrides record drift, production is not touched.

use std::sync::Mutex;

use serde_json::Value;
use stencil_mcp::llm::{
    chat, llm_system_prompt, ChatError, ChatMessage, ImageAttachment, LlmConfig, Provider, Role,
};
use stencil_mcp::llmtransport::{error_code, error_reason, sanitize_detail, LlmError, LlmTransport};

const WIRE_DIR: &str =
    concat!(env!("CARGO_MANIFEST_DIR"), "/../browser/js/config/llm/fixtures/providerWire");
const SANITIZER_CASES: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../browser/js/config/llm/fixtures/sanitizer/cases.json"
);

fn load_array(path: &str) -> Vec<Value> {
    let raw = std::fs::read_to_string(path).unwrap_or_else(|e| panic!("cannot read {path}: {e}"));
    // One browser `expect` deliberately ends in a lone high surrogate (`\ud83d`), which
    // serde_json refuses. Neutralize the escape — that case's expectation is recomputed
    // for mcp via an override, so the browser literal is never compared here.
    let raw = raw.replace("\\ud83d", "\\ufffd");
    serde_json::from_str(&raw).unwrap_or_else(|e| panic!("{path} is not a JSON array: {e}"))
}

fn overrides(family: &str) -> Value {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/fixture_overrides.json");
    let raw = std::fs::read_to_string(path).unwrap_or_else(|e| panic!("cannot read {path}: {e}"));
    serde_json::from_str::<Value>(&raw).expect("fixture_overrides.json parses")[family].clone()
}

// ── providerWire ──

struct Recorded {
    url: String,
    headers: Vec<(String, String)>,
    body: String,
}

/// Capturing transport: records the request, answers with the case's canned response.
/// Non-2xx bodies go through the SAME `error_reason` seam `PlainHttpTransport` uses.
struct MockTransport {
    canned: Result<String, (u16, String)>,
    seen: Mutex<Option<Recorded>>,
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

fn config_for(case: &Value) -> LlmConfig {
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

fn messages_for(case: &Value) -> Vec<ChatMessage> {
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
fn system_slot<'a>(provider: Provider, body: &'a mut Value) -> &'a mut Value {
    match provider {
        Provider::Ollama | Provider::OpenaiCompat => &mut body["messages"][0]["content"],
        Provider::StencilServer => &mut body["system"],
    }
}

fn walk_wire_file(file: &str) {
    let overrides = overrides("providerWire");
    for case in load_array(&format!("{WIRE_DIR}/{file}")) {
        let name = case["name"].as_str().expect("case name");
        let ov = &overrides[name];
        let config = config_for(&case);
        let messages = messages_for(&case);

        let canned = if !case["response"].is_null() {
            Ok(serde_json::to_string(&case["response"]).unwrap())
        } else {
            let er = &case["errorResponse"];
            let status = u16::try_from(er["status"].as_u64().unwrap()).unwrap();
            let body = match &er["body"] {
                Value::String(s) => s.clone(),
                other => serde_json::to_string(other).unwrap(),
            };
            Err((status, body))
        };
        let mock = MockTransport { canned, seen: Mutex::new(None) };

        let outcome = chat(&mock, &config, &messages, "");

        // The request: URL, Authorization presence/value, and the exact JSON body.
        let seen = mock.seen.lock().unwrap();
        let seen = seen.as_ref().unwrap_or_else(|| panic!("[{name}] no request captured"));
        assert_eq!(seen.url, case["expectUrl"].as_str().unwrap(), "[{name}] url");
        let auth = seen
            .headers
            .iter()
            .find(|(k, _)| k.eq_ignore_ascii_case("authorization"))
            .map(|(_, v)| v.as_str());
        assert_eq!(auth, case["expectAuthorization"].as_str(), "[{name}] Authorization header");

        let mut body: Value = serde_json::from_str(&seen.body).unwrap();
        let slot = system_slot(config.provider, &mut body);
        assert_eq!(
            slot.as_str(),
            Some(llm_system_prompt()),
            "[{name}] mcp always sends its own canonical §4 prompt"
        );
        // Substitute the fixture's stand-in system text, then compare the WHOLE body.
        *slot = case["chat"]["system"].clone();
        assert_eq!(body, case["expectBody"], "[{name}] request body");

        // The outcome: reply or typed error; override.kind pins mcp-side divergences.
        let want_kind = ov["kind"].as_str().or(case["expectError"]["kind"].as_str());
        match want_kind {
            None => {
                let reply = outcome.unwrap_or_else(|e| panic!("[{name}] chat failed: {e}"));
                assert_eq!(reply, case["expectReply"].as_str().unwrap(), "[{name}] reply");
            }
            Some("badReply") => {
                assert!(
                    matches!(outcome, Err(ChatError::BadReply(_))),
                    "[{name}] pinned as BadReply, got {outcome:?}"
                );
            }
            Some("disabled") => {
                let Err(ChatError::Disabled(reason)) = outcome else {
                    panic!("[{name}] expected the typed disabled error, got {outcome:?}");
                };
                assert_eq!(reason, case["expectError"]["message"].as_str().unwrap(), "[{name}]");
            }
            Some("truncated") => {
                assert!(
                    matches!(outcome, Err(ChatError::Truncated)),
                    "[{name}] expected the typed truncation error, got {outcome:?}"
                );
            }
            Some("refusal") => {
                let Err(ChatError::Refusal(text)) = outcome else {
                    panic!("[{name}] expected a refusal error, got {outcome:?}");
                };
                assert_eq!(text, case["expectError"]["message"].as_str().unwrap(), "[{name}]");
            }
            Some("http") => {
                let Err(ChatError::Transport(err)) = outcome else {
                    panic!("[{name}] expected an http-status error, got {outcome:?}");
                };
                let LlmError::Status { status, .. } = &err else {
                    panic!("[{name}] expected LlmError::Status, got {err:?}");
                };
                let want_status =
                    u16::try_from(case["expectError"]["status"].as_u64().unwrap()).unwrap();
                assert_eq!(*status, want_status, "[{name}] status");
                // message: mcp's own sanitizer/fallback text where it diverges (override).
                let want_message = ov["message"]
                    .as_str()
                    .or(case["expectError"]["message"].as_str())
                    .unwrap();
                assert_eq!(err.to_string(), want_message, "[{name}] message");
            }
            Some(other) => panic!("[{name}] unhandled error kind {other}"),
        }
    }
}

#[test]
fn wire_ollama() {
    walk_wire_file("ollama.json");
}

#[test]
fn wire_openai() {
    walk_wire_file("openai.json");
}

#[test]
fn wire_server() {
    walk_wire_file("server.json");
}

#[test]
fn wire_http_errors() {
    walk_wire_file("httpErrors.json");
}

// ── sanitizer ──

/// `cases.json` against `sanitize_detail`. `null` input is skipped (a Rust `&str` cannot
/// be null; the transport maps an absent message to `""` before the sanitizer). Every
/// output — pinned or shared — must keep the no-URL and bounded-length invariants.
#[test]
fn sanitizer_cases() {
    let overrides = overrides("sanitizer");
    let mut walked = 0usize;
    let mut failures: Vec<String> = Vec::new();
    for case in load_array(SANITIZER_CASES) {
        let name = case["name"].as_str().expect("case name");
        let Some(input) = case["input"].as_str() else {
            continue; // null input: unrepresentable here, skip per the schema
        };
        walked += 1;
        let got = sanitize_detail(input);
        let want = overrides[name]["expect"].as_str().or(case["expect"].as_str()).unwrap();
        if got != want {
            failures.push(format!("[{name}]\n got: {got:?}\nwant: {want:?}"));
        }
        assert!(!got.contains("://"), "[{name}] no URL may survive");
        assert!(got.chars().count() <= 201, "[{name}] at most 200 chars + ellipsis");
    }
    assert!(walked >= 15, "expected a real corpus, walked {walked}");
    assert!(failures.is_empty(), "sanitizer mismatches:\n{}", failures.join("\n"));
}
