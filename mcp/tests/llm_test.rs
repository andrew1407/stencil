//! Provider config resolution (§5) and the ollama / openai-compat wire shapes (§6.1–§6.2).

mod common;
use common::llm::{env, png_attachment, provider_env, user_message, MockTransport};

use stencil_mcp::config::LlmEnv;
use stencil_mcp::llm::{chat, llm_system_prompt, LlmConfig, Provider};

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
