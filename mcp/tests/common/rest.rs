//! A scripted loopback HTTP server standing in for the collaboration server, and a harness whose
//! allowlist names it — for the suites that run the real CLI's `--server` modes.
use std::io::{Read, Write};
use std::net::TcpListener;
use std::path::Path;
use std::sync::mpsc;
use std::time::Duration;

use stencil_mcp::config::{Config, Servers};
use stencil_mcp::confine::Roots;

use super::dispatch::Harness;

/// Answer one connection per scripted reply, in order, handing back each request — its head
/// lowercased, then its body as sent.
pub fn scripted(replies: Vec<String>) -> (String, mpsc::Receiver<String>) {
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind");
    let origin = format!("http://{}", listener.local_addr().unwrap());
    let (sender, receiver) = mpsc::channel();
    std::thread::spawn(move || {
        for reply in replies {
            let Ok((mut stream, _)) = listener.accept() else { return };
            stream.set_read_timeout(Some(Duration::from_secs(5))).unwrap();
            let mut request = Vec::new();
            let mut block = [0u8; 4096];
            let mut head_end = None;
            while head_end.is_none() || request.len() < head_end.unwrap() + body_length(&request) {
                match stream.read(&mut block) {
                    Ok(0) | Err(_) => break,
                    Ok(n) => request.extend_from_slice(&block[..n]),
                }
                if head_end.is_none() {
                    head_end = request.windows(4).position(|w| w == b"\r\n\r\n").map(|at| at + 4);
                }
            }
            let at = head_end.unwrap_or(request.len());
            let head = String::from_utf8_lossy(&request[..at]).to_ascii_lowercase();
            let _ = sender.send(head + &String::from_utf8_lossy(&request[at..]));
            let _ = stream.write_all(reply.as_bytes());
        }
    });
    (origin, receiver)
}

/// The `Content-Length` a request head declares, 0 when it declares none.
fn body_length(request: &[u8]) -> usize {
    let text = String::from_utf8_lossy(request).to_ascii_lowercase();
    let value = text.split("\r\ncontent-length:").nth(1).and_then(|rest| rest.split("\r\n").next());
    value.and_then(|v| v.trim().parse().ok()).unwrap_or(0)
}

pub fn ok(body: serde_json::Value) -> String {
    reply(200, &body.to_string())
}

/// Any status with a raw body, as the collaboration server answers it.
pub fn reply(status: u16, body: &str) -> String {
    let reason = match status {
        200 => "OK",
        404 => "Not Found",
        409 => "Conflict",
        _ => "Status",
    };
    let len = body.len();
    format!("HTTP/1.1 {status} {reason}\r\nConnection: close\r\nContent-Length: {len}\r\n\r\n{body}")
}

pub fn harness(origin: &str, token: Option<&str>) -> Harness {
    Harness::with_config(Config { servers: allow(origin, token), ..Config::default() })
}

/// The same allowlist, writing inside `root`.
pub fn rooted(origin: &str, token: Option<&str>, root: &Path) -> Harness {
    let roots = Roots::new([root.to_path_buf()]).expect("a root");
    Harness::with_config(Config { servers: allow(origin, token), roots, ..Config::default() })
}

fn allow(origin: &str, token: Option<&str>) -> Servers {
    let tokens = token.map(|t| format!("{origin}={t}"));
    Servers::parse(Some(origin), tokens.as_deref(), &mut Vec::new())
}
