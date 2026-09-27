//! A whole MCP session over an in-process duplex pipe, the client spoken as raw JSON-RPC: a
//! client that offers roots has its writes fenced into them (`roots/list`), and a call carrying
//! a progress token hears each CLI step as `notifications/progress` before its result. A
//! `/bin/sh` stub stands in for the CLI, so this binary sets `STENCIL_CLI`.
#![cfg(unix)]

use std::path::{Path, PathBuf};
use std::sync::OnceLock;
use std::time::Duration;

use serde_json::{json, Value};
use stencil_mcp::config::Config;
use stencil_mcp::confine::Roots;
use stencil_mcp::server::StencilServer;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader, DuplexStream, Lines, ReadHalf, WriteHalf};

/// Two `wrote` lines, as a script with two `@save`s prints them, and the directory it ran in.
fn install_stub() {
    static STUB: OnceLock<PathBuf> = OnceLock::new();
    STUB.get_or_init(|| {
        use std::os::unix::fs::PermissionsExt;
        let path = tempfile::tempdir().unwrap().keep().join("stencil-stub");
        let script = "#!/bin/sh\npwd > spawned-in.txt\n\
                      echo 'wrote a.png (2x2)' >&2\necho 'wrote b.png (3x3)' >&2\n";
        std::fs::write(&path, script).expect("write the stub");
        std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o755)).expect("chmod +x");
        std::env::set_var("STENCIL_CLI", &path);
        path
    });
}

/// The client end of one session; `roots` is what it answers `roots/list` with, and a client
/// with none does not offer the capability at all.
struct Client {
    lines: Lines<BufReader<ReadHalf<DuplexStream>>>,
    write: WriteHalf<DuplexStream>,
    roots: Vec<PathBuf>,
}

impl Client {
    async fn start(operator_root: &Path, roots: &[&Path]) -> Client {
        let roots_config = Roots::new([operator_root.to_path_buf()]).unwrap();
        let server = StencilServer::new(Config { roots: roots_config, ..Config::default() });
        let (ours, theirs) = tokio::io::duplex(1 << 16);
        tokio::spawn(async move {
            if let Ok(running) = rmcp::serve_server(server, theirs).await {
                let _ = running.waiting().await;
            }
        });
        let (read, write) = tokio::io::split(ours);
        let roots: Vec<PathBuf> = roots.iter().map(|r| r.to_path_buf()).collect();
        let offers = if roots.is_empty() { json!({}) } else { json!({ "roots": {} }) };
        let mut client = Client { lines: BufReader::new(read).lines(), write, roots };
        client
            .send(json!({ "jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
                "protocolVersion": "2025-06-18", "capabilities": offers,
                "clientInfo": { "name": "wire-test", "version": "0" } } }))
            .await;
        let init = client.next().await;
        assert_eq!(init["result"]["serverInfo"]["name"], "stencil-mcp", "{init}");
        client.send(json!({ "jsonrpc": "2.0", "method": "notifications/initialized" })).await;
        client
    }

    async fn send(&mut self, message: Value) {
        let line = format!("{message}\n");
        self.write.write_all(line.as_bytes()).await.expect("the server is listening");
    }

    async fn next(&mut self) -> Value {
        let line = tokio::time::timeout(Duration::from_secs(30), self.lines.next_line()).await;
        let line = line.expect("the server went quiet").unwrap().expect("the server hung up");
        serde_json::from_str(&line).expect("one JSON-RPC message per line")
    }

    /// One `tools/call`, answering every `roots/list` on the way: its result, and each
    /// progress notification that reached the client before it.
    async fn call(&mut self, id: u64, arguments: Value, token: Option<&str>) -> (Value, Vec<Value>) {
        let mut params = json!({ "name": "stencil_script", "arguments": arguments });
        if let Some(token) = token {
            params["_meta"] = json!({ "progressToken": token });
        }
        self.send(json!({ "jsonrpc": "2.0", "id": id, "method": "tools/call", "params": params }))
            .await;
        let mut progress = Vec::new();
        loop {
            let message = self.next().await;
            match message["method"].as_str() {
                Some("roots/list") => {
                    let roots: Vec<Value> =
                        self.roots.iter().map(|r| json!({ "uri": file_uri(r) })).collect();
                    let reply = json!({ "jsonrpc": "2.0", "id": message["id"],
                                        "result": { "roots": roots } });
                    self.send(reply).await;
                }
                Some("notifications/progress") => progress.push(message["params"].clone()),
                Some(_) => {}
                None if message["id"] == id => return (message["result"].clone(), progress),
                None => {}
            }
        }
    }
}

fn file_uri(dir: &Path) -> String {
    format!("file://{}", dir.display().to_string().replace('%', "%25").replace(' ', "%20"))
}

fn script(output_dir: &str) -> Value {
    json!({ "script_text": "@save a.png\n@save b.png\n", "output_dir": output_dir })
}

fn same_dir(a: &Path, b: &Path) -> bool {
    a.canonicalize().unwrap() == b.canonicalize().unwrap()
}

#[tokio::test]
async fn a_client_that_offers_roots_has_its_writes_fenced_into_them() {
    install_stub();
    let (operator, client_root) = (tempfile::tempdir().unwrap(), tempfile::tempdir().unwrap());
    let mut client = Client::start(operator.path(), &[client_root.path()]).await;

    let (result, _) = client.call(1, script("out"), None).await;
    assert_eq!(result["isError"], false, "{result}");
    let out = client_root.path().join("out");
    let written = &result["structuredContent"]["files"];
    assert_eq!(written[0]["path"], out.join("a.png").to_string_lossy().as_ref(), "{result}");
    let spawned_in = std::fs::read_to_string(out.join("spawned-in.txt")).expect("the stub ran there");
    assert!(same_dir(Path::new(spawned_in.trim()), &out), "spawned in {spawned_in}");

    // The operator's root is no root of this client's: a write there is refused unrun.
    let elsewhere = operator.path().to_string_lossy().into_owned();
    let (refused, _) = client.call(2, script(&elsewhere), None).await;
    assert_eq!(refused["isError"], true, "{refused}");
    let message = refused["content"][0]["text"].as_str().unwrap();
    let listed = format!("outside the allowed roots ({})", client_root.path().display());
    assert!(message.contains(&listed), "{message}");
    assert!(!operator.path().join("spawned-in.txt").exists(), "the refused run was spawned");

    // A client that offers no roots writes inside the operator's.
    let mut plain = Client::start(operator.path(), &[]).await;
    let (result, _) = plain.call(1, script("out"), None).await;
    let fallback = operator.path().join("out").join("a.png");
    assert_eq!(result["structuredContent"]["files"][0]["path"], fallback.to_string_lossy().as_ref());
}

#[tokio::test]
async fn a_call_with_a_progress_token_hears_each_step_before_its_result() {
    install_stub();
    let root = tempfile::tempdir().unwrap();
    let mut client = Client::start(root.path(), &[root.path()]).await;

    let (result, progress) = client.call(7, script("out"), Some("tok-7")).await;
    assert_eq!(result["isError"], false, "{result}");
    assert_eq!(progress.len(), 2, "{progress:?}");
    let steps = ["wrote a.png (2x2)", "wrote b.png (3x3)"];
    for (step, (note, message)) in progress.iter().zip(steps).enumerate() {
        assert_eq!(note["progressToken"], "tok-7");
        assert_eq!(note["progress"], json!(step as f64 + 1.0), "ordered steps");
        assert_eq!(note["message"], message);
    }

    let (_, unasked) = client.call(8, script("again"), None).await;
    assert!(unasked.is_empty(), "no token, no notifications: {unasked:?}");
}
