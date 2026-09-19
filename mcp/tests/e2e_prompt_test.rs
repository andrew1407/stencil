//! The §7 edge map and the whole `stencil_prompt` flow against the real CLI and a canned LLM.
mod common;
use common::e2e::{cli_present, FIXTURE};

use std::io::{Read, Write};
use std::net::TcpListener;

use serde_json::json;
use stencil_mcp::config::LlmEnv;
use stencil_mcp::llm::{self, ChatMessage, LlmConfig, Role};
use stencil_mcp::llmtransport::PlainHttpTransport;
use stencil_mcp::{opplan, pipeline};

/// §7 edge map: the CLI's contour filter renders the input into an attachable PNG.
#[tokio::test]
async fn edge_map_renders_the_contour_filter_via_the_cli() {
    if !cli_present() {
        return;
    }
    let bytes = pipeline::render_edge_map(FIXTURE)
        .await
        .expect("the contour render succeeds");
    assert!(bytes.starts_with(&[0x89, b'P', b'N', b'G']), "a PNG comes back");
    let attachment = llm::edge_map_attachment(&bytes).expect("within the 8 MiB cap");
    assert_eq!(attachment.media_type, "image/png");
}

/// A source the CLI cannot render (a missing file) silently yields no edge map.
#[tokio::test]
async fn edge_map_render_failure_is_a_silent_none() {
    if !cli_present() {
        return;
    }
    assert!(pipeline::render_edge_map("/nonexistent/input.png").await.is_none());
}

/// The full `stencil_prompt` flow against the real CLI and a canned local "ollama" the test
/// starts itself. It self-skips without the CLI binary.
#[tokio::test]
async fn prompt_flow_against_a_canned_llm_and_the_real_cli() {
    if !cli_present() {
        return;
    }

    // The plan the "model" answers with: rotate right, plus one sepia variant.
    let plan_json = json!({
        "version": 1,
        "reply": "Rotated it; the sepia take is separate.",
        "actions": [{"op": "rotate", "dir": "right"}],
        "variants": [{"label": "Sepia Tone", "actions": [{"op": "filter", "mode": "sepia"}]}]
    })
    .to_string();
    let ollama_body = json!({"message": {"role": "assistant", "content": plan_json}}).to_string();

    // A one-shot canned ollama endpoint on an ephemeral port.
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind an ephemeral port");
    let base_url = format!("http://{}", listener.local_addr().unwrap());
    std::thread::spawn(move || {
        let (mut stream, _) = listener.accept().expect("accept");
        let mut buf = [0u8; 65536];
        let mut request: Vec<u8> = Vec::new();
        // Read until the request body (Content-Length arithmetic is overkill here: the
        // client writes the whole request before reading, so read until the JSON closes).
        loop {
            let n = stream.read(&mut buf).expect("read request");
            request.extend_from_slice(&buf[..n]);
            if n == 0 || request.ends_with(b"}") {
                break;
            }
        }
        let head = String::from_utf8_lossy(&request);
        assert!(head.starts_with("POST /api/chat HTTP/1.1\r\n"), "{head}");
        let response = format!(
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\n\r\n{}",
            ollama_body.len(),
            ollama_body
        );
        stream.write_all(response.as_bytes()).expect("write response");
    });

    // 1. Chat through the real plain-http transport.
    let env = LlmEnv {
        provider: Some("ollama".into()),
        base_url: Some(base_url),
        ..LlmEnv::default()
    };
    let config = LlmConfig::resolve(&env, Some("test-model")).unwrap();
    let messages = vec![ChatMessage {
        role: Role::User,
        text: "rotate it right and give me a sepia variant".to_string(),
        images: Vec::new(),
    }];
    let reply = tokio::task::spawn_blocking(move || {
        llm::chat(&PlainHttpTransport::new(), &config, &messages, "")
    })
    .await
    .unwrap()
    .expect("chat should succeed");

    // 2. Parse and map the plan.
    let plan = opplan::parse_op_plan(&reply).expect("plan should validate");
    assert_eq!(plan.reply, "Rotated it; the sepia take is separate.");
    let dir = tempfile::tempdir().unwrap();
    let out_dir = dir.path().to_string_lossy().into_owned();
    let requests = opplan::to_edit_requests(
        &plan,
        Some(FIXTURE),
        &out_dir,
        &mut Vec::new(),
    ).expect("mappable");
    assert_eq!(requests.len(), 2);

    // 3. Execute through the real CLI: base = rotated (dims swap), variant = rotated+sepia.
    let base = pipeline::run_edit(&requests[0].params)
        .await
        .expect("base edit should succeed");
    assert_eq!((base.width, base.height), (12, 16));
    assert!(base.path.ends_with("result.png"), "{}", base.path);

    let variant = pipeline::run_edit(&requests[1].params)
        .await
        .expect("variant edit should succeed");
    assert_eq!((variant.width, variant.height), (12, 16));
    assert!(variant.path.ends_with("sepia-tone.png"), "{}", variant.path);
    assert!(std::path::Path::new(&variant.path).exists());
}
