//! Walk the shared LLM wire fixtures through mcp's real client, one reported case per
//! fixture:
//!
//! - `browser/js/config/llm/fixtures/providerWire/` — build each request via `llm::chat`
//!   against a capturing mock transport, deep-compare the JSON body (absence is contract),
//!   then feed the canned response/error and check the extracted reply / typed error.
//!   mcp hard-codes its own §4 canonical system prompt (the fixtures carry the browser's
//!   short stand-in), so the walker first asserts mcp sent exactly `llm_system_prompt()`
//!   and then substitutes the fixture's system text before the deep compare.
//!
//! This phase PINS current behavior — overrides record drift, production is not touched.
use std::sync::{LazyLock, Mutex};

use serde_json::Value;
use stencil_mcp::llm::{chat, llm_system_prompt, ChatError};
use stencil_mcp::llmtransport::LlmError;

mod common;
use common::walk::Walk;
use common::wire::{config_for, load_array, messages_for, system_slot, MockTransport};

const WIRE_DIR: &str =
    concat!(env!("CARGO_MANIFEST_DIR"), "/../browser/js/config/llm/fixtures/providerWire");

const WIRE_FILES: [&str; 4] = ["ollama.json", "openai.json", "server.json", "httpErrors.json"];

static CORPUS: LazyLock<Vec<(&'static str, Vec<Value>)>> = LazyLock::new(|| {
    WIRE_FILES.iter().map(|f| (*f, load_array(&format!("{WIRE_DIR}/{f}")))).collect()
});

fn check_case(case: &Value) {
    let name = case["name"].as_str().expect("case name");
    let ov = &common::overrides("providerWire")[name];
    let config = config_for(case);
    let messages = messages_for(case);

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

fn main() {
    let mut walk = Walk::new();
    for (file, cases) in &*CORPUS {
        let family = file.trim_end_matches(".json");
        for case in cases {
            let name = case["name"].as_str().expect("case name");
            walk.case(format!("{family}/{name}"), || check_case(case));
        }
    }
    walk.run()
}
