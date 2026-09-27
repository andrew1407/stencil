//! A cancelled call takes its CLI child and its LLM request with it, and every child runs on
//! the allowlisted environment. A `/bin/sh` stub stands in for the CLI, so this binary sets
//! `STENCIL_CLI`; a silent loopback endpoint stands in for the model.
#![cfg(unix)]

mod common;
use common::dispatch::{text_of, Harness};

use std::io::Read;
use std::path::Path;
use std::time::{Duration, Instant};

use rmcp::ServerHandler;
use serde_json::json;
use stencil_mcp::config::{Config, LlmEnv};
use stencil_mcp::confine::Roots;

/// Dump the environment, sleep past the test's patience, then leave a marker — which only
/// ever appears if the child outlived its call.
fn stub(dir: &Path) -> std::path::PathBuf {
    use std::os::unix::fs::PermissionsExt;
    let path = dir.join("stencil-stub");
    let (env, marker) = (dir.join("env.txt"), dir.join("survived"));
    let (env, marker) = (env.display(), marker.display());
    let script = format!("#!/bin/sh\nenv > \"{env}\"\nsleep 2\n: > \"{marker}\"\n");
    std::fs::write(&path, script).expect("write the stub");
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o755)).expect("chmod +x");
    path
}

#[tokio::test]
async fn a_cancelled_call_kills_its_cli_child_which_never_saw_the_secrets() {
    let dir = tempfile::tempdir().unwrap();
    std::env::set_var("STENCIL_CLI", stub(dir.path()));
    std::env::set_var("STENCIL_LLM_API_KEY", "sk-must-not-leak");
    std::env::set_var("STENCIL_MCP_SERVER_TOKENS", "http://h=must-not-leak");

    let h = Harness::rooted(dir.path());
    let context = h.context();
    let cancel = context.ct.clone();
    let request = serde_json::from_value(json!({
        "name": "stencil_script",
        "arguments": { "script_text": "@save x\n", "output_dir": dir.path() },
    }))
    .unwrap();
    // Cancel once the stub has started (a loaded machine can take far longer than a fixed
    // delay to spawn it), then time the return from the cancel, not from the call.
    let env_file = dir.path().join("env.txt");
    let (result, cancelled_at) = tokio::join!(h.server.call_tool(request, context), async {
        let deadline = Instant::now() + Duration::from_secs(20);
        while !env_file.exists() && Instant::now() < deadline {
            tokio::time::sleep(Duration::from_millis(20)).await;
        }
        cancel.cancel();
        Instant::now()
    });

    let result = result.expect("a tool result");
    assert!(text_of(&result).contains("cancelled"), "got: {}", text_of(&result));
    assert!(cancelled_at.elapsed() < Duration::from_millis(1500), "took {:?}", cancelled_at.elapsed());

    tokio::time::sleep(Duration::from_secs(3)).await;
    assert!(!dir.path().join("survived").exists(), "the cancelled CLI kept running");
    let env = std::fs::read_to_string(dir.path().join("env.txt")).expect("the stub ran");
    assert!(env.contains("NO_COLOR=1") && env.contains("PATH="), "{env}");
    assert!(!env.contains("must-not-leak"), "a secret reached the child:\n{env}");
}

/// The endpoint takes the request and never answers; it stamps when its connection closes,
/// which only the abort does before the request's own deadline.
#[tokio::test]
async fn a_cancelled_prompt_closes_its_llm_request_at_once() {
    let dir = tempfile::tempdir().unwrap();
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let base_url = format!("http://{}", listener.local_addr().unwrap());
    let (stamp, stamps) = std::sync::mpsc::channel();
    std::thread::spawn(move || {
        let (mut stream, _) = listener.accept().unwrap();
        stream.set_read_timeout(Some(Duration::from_secs(40))).unwrap();
        let mut block = [0u8; 4096];
        let _ = stream.read(&mut block);
        let _ = stamp.send(Instant::now());
        while matches!(stream.read(&mut block), Ok(n) if n > 0) {}
        let _ = stamp.send(Instant::now());
    });

    let llm = LlmEnv { provider: Some("ollama".into()), base_url: Some(base_url), ..LlmEnv::default() };
    let roots = Roots::new([dir.path().to_path_buf()]).unwrap();
    let h = common::dispatch::Harness::with_config(Config { roots, llm, ..Config::default() });
    let context = h.context();
    let cancel = context.ct.clone();
    let request = serde_json::from_value(json!({
        "name": "stencil_prompt",
        "arguments": { "prompt": "hi", "output_dir": dir.path() },
    }))
    .unwrap();
    let (result, cancelled_at) = tokio::join!(h.server.call_tool(request, context), async {
        let deadline = Instant::now() + Duration::from_secs(20);
        while stamps.try_recv().is_err() && Instant::now() < deadline {
            tokio::time::sleep(Duration::from_millis(20)).await;
        }
        cancel.cancel();
        Instant::now()
    });

    let result = result.expect("a tool result");
    assert!(text_of(&result).contains("cancelled"), "got: {}", text_of(&result));
    let closed_at = stamps.recv_timeout(Duration::from_secs(20)).expect("the request was ended");
    let late = closed_at.saturating_duration_since(cancelled_at);
    assert!(late < Duration::from_secs(2), "closed {late:?} after the cancel");
}
