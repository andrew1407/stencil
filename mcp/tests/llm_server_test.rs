//! The stencil-server wire shape (§6.3), attachment order, stop reasons, malformed replies.

mod common;
use common::llm::{env, png_attachment, server_env, user_message, MockTransport};

use stencil_mcp::config::LlmEnv;
use stencil_mcp::llm::{
    chat, llm_system_prompt, ChatError, ImageAttachment, LlmConfig,
};

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
