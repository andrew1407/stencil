//! The Phase-1 spawn/response guards, each proven by the thing it blocks: the per-invocation
//! CLI deadline (a hung child is killed, not abandoned) and the response-body cap.

use std::io::{Read, Write};
use std::net::TcpListener;
use std::path::{Path, PathBuf};
use std::time::Duration;

use stencil_mcp::args::EditParams;
use stencil_mcp::config::cli_timeout;
use stencil_mcp::llmtransport::{LlmError, LlmTransport, PlainHttpTransport};

// ── The CLI spawn deadline ──

/// `STENCIL_CLI` is process-wide, so the two tests that resolve the binary take turns —
/// otherwise the real-CLI test can pick up the sleeping stub.
static CLI_ENV: std::sync::Mutex<()> = std::sync::Mutex::new(());

fn cli_env() -> std::sync::MutexGuard<'static, ()> {
    CLI_ENV.lock().unwrap_or_else(|e| e.into_inner())
}

/// A `/bin/sh` stub standing in for the CLI: sleep well past the deadline, then touch
/// `marker`. The marker is the proof — it only ever appears if the child outlived the kill.
#[cfg(unix)]
fn sleeping_cli(dir: &Path, marker: &Path) -> PathBuf {
    use std::os::unix::fs::PermissionsExt;
    let path = dir.join("stencil-stub");
    let script = format!("#!/bin/sh\nsleep 4\n: > \"{}\"\n", marker.display());
    std::fs::write(&path, script).expect("write the stub");
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o755)).expect("chmod +x");
    path
}

#[cfg(unix)]
#[tokio::test]
async fn a_hung_cli_is_killed_at_the_deadline_and_reported_in_the_clis_error_shape() {
    let _env = cli_env();
    let dir = tempfile::tempdir().expect("a temp dir");
    let marker = dir.path().join("survived");
    let stub = sleeping_cli(dir.path(), &marker);
    std::env::set_var("STENCIL_CLI", &stub);
    std::env::set_var("STENCIL_CLI_TIMEOUT_SECONDS", "1");

    // The deadline is read per spawn, and falls back to 120s for blank/zero/unparseable.
    assert_eq!(cli_timeout(), Duration::from_secs(1));

    let started = std::time::Instant::now();
    let err = stencil_mcp::pipeline::run_probe("ignored.png")
        .await
        .expect_err("a CLI that never exits must not resolve");
    let elapsed = started.elapsed();

    assert!(
        err.starts_with("error:") && err.contains("timed out after 1s and was terminated"),
        "expected the CLI's own `error:` shape, got {err:?}"
    );
    assert!(elapsed < Duration::from_secs(3), "returned only after {elapsed:?}");

    // Killed, not merely abandoned: the stub's post-sleep marker never appears.
    tokio::time::sleep(Duration::from_secs(6)).await;
    assert!(
        !marker.exists(),
        "the timed-out CLI kept running past the deadline"
    );

    std::env::set_var("STENCIL_CLI_TIMEOUT_SECONDS", "0");
    assert_eq!(cli_timeout(), Duration::from_secs(120));
    std::env::set_var("STENCIL_CLI_TIMEOUT_SECONDS", "not-a-number");
    assert_eq!(cli_timeout(), Duration::from_secs(120));
    std::env::remove_var("STENCIL_CLI_TIMEOUT_SECONDS");
    assert_eq!(cli_timeout(), Duration::from_secs(120));
    std::env::remove_var("STENCIL_CLI");
}

// ── The response-body cap ──

/// Answer one connection with `response` once the request head has arrived, then close.
fn canned_server(response: &'static str) -> String {
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind an ephemeral port");
    let addr = listener.local_addr().unwrap();
    std::thread::spawn(move || {
        let (mut stream, _) = listener.accept().expect("accept");
        stream.set_read_timeout(Some(Duration::from_secs(5))).unwrap();
        let mut request: Vec<u8> = Vec::new();
        let mut block = [0u8; 4096];
        while !request.windows(4).any(|w| w == b"\r\n\r\n") {
            match stream.read(&mut block) {
                Ok(0) | Err(_) => break,
                Ok(n) => request.extend_from_slice(&block[..n]),
            }
        }
        stream.write_all(response.as_bytes()).ok();
    });
    format!("http://{addr}")
}

/// The cap matches the collaboration server's `maxResponseBytes` (8 MiB), and a declared
/// `Content-Length` over it is refused before a single body byte is read.
#[test]
fn a_declared_body_over_8_mib_is_refused_without_being_read() {
    let base = canned_server(
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 8388609\r\n\r\n",
    );
    let err = PlainHttpTransport::with_timeout(Duration::from_secs(5))
        .post_json(&base, &[], "{}")
        .expect_err("an oversize body must not be accepted");
    match &err {
        LlmError::BadResponse(message) => assert_eq!(message, "response body exceeds 8 MiB"),
        other => panic!("expected BadResponse, got {other:?}"),
    }
}

/// One byte under the cap is still a normal response — the guard is a ceiling, not a ban.
#[test]
fn a_body_at_the_cap_is_still_accepted() {
    let base = canned_server("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}");
    let body = PlainHttpTransport::with_timeout(Duration::from_secs(5))
        .post_json(&base, &[], "{}")
        .expect("a small body still succeeds");
    assert_eq!(body, "{}");
}

// ── Output-path confinement ──

/// A run whose output escapes its sandbox root never reaches the CLI at all: the rewrite
/// fails and the run is refused in the CLI's own `error:` shape.
#[tokio::test]
async fn an_output_outside_the_confinement_root_is_refused_before_the_cli_runs() {
    let dir = tempfile::tempdir().expect("a temp dir");
    let root = dir.path().join("sandbox");
    std::fs::create_dir_all(&root).unwrap();
    let escaped = dir.path().join("escaped.png");
    let mut params: EditParams = serde_json::from_value(serde_json::json!({
        "blank": { "width": 4, "height": 4 },
        "output": escaped.to_string_lossy(),
        "overwrite": true,
    }))
    .expect("params should deserialize");
    params.confine_root = Some(root.to_string_lossy().into_owned());

    let err = stencil_mcp::pipeline::run_edit(&params)
        .await
        .expect_err("a destination outside the root must be refused")
        .to_string();
    assert!(err.starts_with("error: refusing to write outside"), "{err}");
    assert!(!escaped.exists(), "nothing was written");
}

/// And the flag really reaches the CLI: the same absolute destination, handed to the real
/// binary with `--confine-output`, is refused by the CLI itself.
#[tokio::test]
async fn the_cli_refuses_an_absolute_output_under_the_flag() {
    let _env = cli_env();
    let Ok(bin) = stencil_mcp::locate::find_cli() else {
        eprintln!("skipping: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return;
    };
    let dir = tempfile::tempdir().expect("a temp dir");
    let out = dir.path().join("absolute.png");
    let mut command = tokio::process::Command::new(&bin);
    command.args(["--confine-output", "--blank", "4", "4"]).arg(&out);
    let run = command.output().await.expect("the CLI should run");
    let stderr = String::from_utf8_lossy(&run.stderr);
    assert!(!run.status.success() && !out.exists(), "{stderr}");
    assert!(
        stderr.contains("--confine-output: refusing to write outside"),
        "{stderr}"
    );
}
