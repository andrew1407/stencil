//! What a CLI child is given and what is kept of it: the environment allowlist, the bounded
//! stderr tail and stdout cap, and the progress lines a call's reports reach.

use std::ffi::OsString;
use std::sync::{Arc, Mutex};

use stencil_mcp::pipeline::capture::{read_capped, read_lines, Tail};
use stencil_mcp::pipeline::{child_env, gui_env};
use stencil_mcp::pipeline::progress::{self, Sink};

fn vars(pairs: &[(&str, &str)]) -> Vec<(OsString, OsString)> {
    pairs.iter().map(|(k, v)| (OsString::from(k), OsString::from(v))).collect()
}

/// A child keeps what running a program needs and nothing of the LLM or server secrets.
#[test]
fn the_child_environment_is_an_allowlist() {
    let kept = child_env(
        vars(&[
            ("PATH", "/bin"),
            ("HOME", "/home/u"),
            ("LC_ALL", "C"),
            ("HTTPS_PROXY", "http://proxy:3128"),
            ("DEVELOPER_DIR", "/Applications/Xcode.app"),
            ("STENCIL_LLM_API_KEY", "sk-secret"),
            ("STENCIL_MCP_SERVER_TOKENS", "http://h=tok"),
            ("AWS_SECRET_ACCESS_KEY", "x"),
        ])
        .into_iter(),
    );
    let names: Vec<&str> = kept.iter().map(|(k, _)| k.to_str().unwrap()).collect();
    assert_eq!(names, ["PATH", "HOME", "LC_ALL", "HTTPS_PROXY", "DEVELOPER_DIR"]);
}

/// A GUI launch keeps the display, session bus and profile a desktop app needs — Windows
/// spellings included — and still none of the secrets.
#[test]
fn the_gui_environment_adds_the_session_and_keeps_out_the_secrets() {
    let kept = gui_env(
        vars(&[
            ("Path", "C:\\Windows"),
            ("windir", "C:\\Windows"),
            ("DISPLAY", ":0"),
            ("XDG_RUNTIME_DIR", "/run/user/1000"),
            ("QT_QPA_PLATFORM", "wayland"),
            ("DBUS_SESSION_BUS_ADDRESS", "unix:path=/run/bus"),
            ("__CF_USER_TEXT_ENCODING", "0x1F5:0x0:0x0"),
            ("LC_ALL", "C"),
            ("STENCIL_LLM_API_KEY", "sk-secret"),
            ("STENCIL_MCP_SERVER_TOKENS", "http://h=tok"),
            ("STENCIL_SERVER_TOKENS", "http://h=tok"),
            ("GITHUB_TOKEN", "x"),
        ])
        .into_iter(),
    );
    let names: Vec<&str> = kept.iter().map(|(k, _)| k.to_str().unwrap()).collect();
    let expected = ["Path", "windir", "DISPLAY", "XDG_RUNTIME_DIR", "QT_QPA_PLATFORM",
        "DBUS_SESSION_BUS_ADDRESS", "__CF_USER_TEXT_ENCODING", "LC_ALL"];
    assert_eq!(names, expected);
}

#[test]
fn the_tail_keeps_the_newest_whole_lines_within_its_bound() {
    let mut tail = Tail::new(13);
    for line in ["first", "second", "third"] {
        tail.push(line.to_string());
    }
    assert_eq!(tail.into_text(), "second\nthird\n");
}

#[tokio::test]
async fn lines_are_split_reported_and_bounded() {
    let seen = Mutex::new(Vec::new());
    let text = read_lines(&b"wrote a.png (1x1)\r\nnote: x\nno newline"[..], 1024, |line| {
        seen.lock().unwrap().push(line.to_string())
    })
    .await;
    assert_eq!(text, "wrote a.png (1x1)\nnote: x\nno newline\n");
    assert_eq!(seen.into_inner().unwrap(), ["wrote a.png (1x1)", "note: x", "no newline"]);

    let long = vec![b'x'; 200 * 1024];
    let bounded = read_lines(&long[..], 4096, |_| {}).await;
    assert!(bounded.len() <= 70 * 1024, "one runaway line was cut: {} bytes", bounded.len());
}

#[tokio::test]
async fn stdout_past_its_cap_is_refused() {
    assert_eq!(read_capped(&b"{}"[..], 16).await.unwrap(), "{}");
    let error = read_capped(&vec![b'x'; 3 << 20][..], 2 << 20).await.unwrap_err();
    assert!(error.contains("more than 2 MiB"), "{error}");
}

/// A child still writing past the cap is drained to its end, so it exits on its own (not by a
/// broken pipe, and not at the run deadline) and the refusal comes back at once.
#[cfg(unix)]
#[tokio::test]
async fn a_child_writing_past_the_cap_is_drained_to_its_exit() {
    let mut child = tokio::process::Command::new("sh")
        .args(["-c", "head -c 4194304 /dev/zero"])
        .stdout(std::process::Stdio::piped())
        .kill_on_drop(true)
        .spawn()
        .expect("sh runs");
    let pipe = child.stdout.take().unwrap();
    let run = async {
        let refused = read_capped(pipe, 1 << 20).await.unwrap_err();
        (refused, child.wait().await.unwrap())
    };
    let (refused, status) = tokio::time::timeout(std::time::Duration::from_secs(10), run)
        .await
        .expect("the capped read returned and the child exited");
    assert!(refused.contains("more than 1 MiB"), "{refused}");
    assert!(status.success(), "the child was cut off instead of finishing: {status}");
}

/// A report reaches the sink its task is scoped to, spawned tasks included once re-scoped;
/// outside any scope it is dropped.
#[tokio::test]
async fn progress_reports_reach_the_scoped_sink() {
    let got = Arc::new(Mutex::new(Vec::new()));
    let sink = {
        let got = got.clone();
        Sink::new(move |message| got.lock().unwrap().push(message))
    };
    progress::report("before any scope");
    progress::scope(Some(sink), async {
        progress::report("in the call");
        let inherited = progress::current();
        tokio::spawn(progress::scope(inherited, async { progress::report("in a spawned run") }))
            .await
            .unwrap();
    })
    .await;
    assert_eq!(*got.lock().unwrap(), ["in the call", "in a spawned run"]);
}
