//! The hand-rolled plain-http transport, tested against a canned `std::net::TcpListener`
//! on an ephemeral port — no external network, no CLI binary.
use std::io::{Read, Write};
use std::net::TcpListener;
use std::sync::mpsc;
use std::time::Duration;

use stencil_mcp::llmtransport::{LlmError, LlmTransport, PlainHttpTransport};

/// Spawn a one-shot HTTP server: accept one connection, read the full request, send
/// `response` verbatim, close. The raw request bytes arrive on the returned channel.
fn canned_server(response: &'static str) -> (String, mpsc::Receiver<Vec<u8>>) {
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind an ephemeral port");
    let addr = listener.local_addr().unwrap();
    let (tx, rx) = mpsc::channel();
    std::thread::spawn(move || {
        let (mut stream, _) = listener.accept().expect("accept");
        stream
            .set_read_timeout(Some(Duration::from_secs(5)))
            .unwrap();

        // Read headers, then exactly Content-Length body bytes.
        let mut request: Vec<u8> = Vec::new();
        let mut block = [0u8; 4096];
        let header_end = loop {
            let n = stream.read(&mut block).expect("read request");
            assert!(n > 0, "client closed before finishing the request");
            request.extend_from_slice(&block[..n]);
            if let Some(i) = request
                .windows(4)
                .position(|w| w == b"\r\n\r\n")
            {
                break i;
            }
        };
        let head = String::from_utf8_lossy(&request[..header_end]).to_string();
        let content_length: usize = head
            .lines()
            .find_map(|l| {
                let (name, value) = l.split_once(':')?;
                name.trim()
                    .eq_ignore_ascii_case("content-length")
                    .then(|| value.trim().parse().ok())?
            })
            .unwrap_or(0);
        while request.len() < header_end + 4 + content_length {
            let n = stream.read(&mut block).expect("read body");
            assert!(n > 0, "client closed mid-body");
            request.extend_from_slice(&block[..n]);
        }

        stream.write_all(response.as_bytes()).expect("write response");
        drop(stream); // Connection: close
        tx.send(request).ok();
    });
    (format!("http://{addr}"), rx)
}

fn transport() -> PlainHttpTransport {
    PlainHttpTransport::with_timeout(Duration::from_secs(5))
}

#[test]
fn posts_a_wellformed_request_and_reads_a_content_length_body() {
    let (base, rx) = canned_server(
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\n\r\n{\"ok\":true}",
    );
    let headers = vec![("Authorization".to_string(), "Bearer secret".to_string())];
    let body = r#"{"model":"m"}"#;
    let result = transport()
        .post_json(&format!("{base}/api/chat"), &headers, body)
        .expect("2xx should succeed");
    assert_eq!(result, "{\"ok\":true}");

    let request = String::from_utf8(rx.recv().unwrap()).unwrap();
    let mut lines = request.split("\r\n");
    assert_eq!(lines.next().unwrap(), "POST /api/chat HTTP/1.1");
    assert!(request.contains("\r\nHost: 127.0.0.1:"), "{request}");
    assert!(request.contains("\r\nContent-Type: application/json\r\n"));
    assert!(request.contains(&format!("\r\nContent-Length: {}\r\n", body.len())));
    assert!(request.contains("\r\nConnection: close\r\n"));
    assert!(request.contains("\r\nAuthorization: Bearer secret\r\n"));
    assert!(request.ends_with(&format!("\r\n\r\n{body}")), "{request}");
}

#[test]
fn decodes_a_chunked_body() {
    // "hello " + "world" split across two chunks, with a chunk extension and trailers.
    let (base, _rx) = canned_server(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n6;ext=1\r\nhello \r\n5\r\nworld\r\n0\r\nTrailer: x\r\n\r\n",
    );
    let result = transport()
        .post_json(&format!("{base}/chat"), &[], "{}")
        .expect("chunked 2xx should succeed");
    assert_eq!(result, "hello world");
}

#[test]
fn reads_to_eof_when_no_framing_is_declared() {
    let (base, _rx) = canned_server("HTTP/1.1 200 OK\r\n\r\nplain tail");
    let result = transport().post_json(&base, &[], "{}").unwrap();
    assert_eq!(result, "plain tail");
}

#[test]
fn non_2xx_surfaces_the_providers_own_reason() {
    let (base, _rx) = canned_server(
        "HTTP/1.1 404 Not Found\r\nContent-Length: 26\r\n\r\n{\"error\":\"model missing\"}\n",
    );
    let err = transport().post_json(&base, &[], "{}").unwrap_err();
    match &err {
        LlmError::Status { status, reason, .. } => {
            assert_eq!(*status, 404);
            assert_eq!(reason, "model missing");
        }
        other => panic!("expected a status error, got {other:?}"),
    }
    // Contract §6.3: the reason is said once — no status restating it, no raw body.
    assert_eq!(err.to_string(), "model missing");
}

#[test]
fn a_recognised_upstream_reason_is_all_the_message_says() {
    let (base, _rx) = canned_server(
        "HTTP/1.1 502 Bad Gateway\r\n\r\n{\"code\":\"llmUpstream\",\"message\":\"the LLM \
         provider is out of credits or has no active billing\"}",
    );
    let err = transport().post_json(&base, &[], "{}").unwrap_err();
    assert_eq!(
        err.to_string(),
        "the LLM provider is out of credits or has no active billing"
    );
}

#[test]
fn a_body_that_explains_nothing_falls_back_to_the_bare_status() {
    let (base, _rx) = canned_server("HTTP/1.1 500 Server Error\r\n\r\n<html>oops</html>");
    let err = transport().post_json(&base, &[], "{}").unwrap_err();
    assert_eq!(err.to_string(), "the LLM endpoint answered HTTP 500");
}

#[test]
fn provider_prose_is_bounded_and_never_echoes_a_key_or_url() {
    let (base, _rx) = canned_server(
        "HTTP/1.1 401 Unauthorized\r\n\r\n{\"error\":{\"message\":\"Incorrect API key provided: \
         sk-abcdef1234567890\\nsee http://lm.local/keys\"}}",
    );
    let err = transport().post_json(&base, &[], "{}").unwrap_err();
    assert_eq!(
        err.to_string(),
        "Incorrect API key provided: [redacted] see [redacted]"
    );
}

#[test]
fn https_and_other_schemes_are_rejected_with_the_plain_http_message() {
    for url in [
        "https://api.example.com/v1/chat",
        "ftp://host/x",
        "localhost:11434",
    ] {
        let err = transport().post_json(url, &[], "{}").unwrap_err();
        assert!(
            matches!(err, LlmError::UnsupportedScheme(_)),
            "{url} → {err:?}"
        );
        let message = err.to_string();
        assert!(
            message.contains("mcp's built-in transport is plain-http; use a local provider or an http:// server"),
            "{url} → {message}"
        );
    }
}

#[test]
fn connection_refused_is_a_connect_error() {
    // Bind + drop to get a port that is (very likely) closed.
    let addr = {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        listener.local_addr().unwrap()
    };
    let err = transport()
        .post_json(&format!("http://{addr}/x"), &[], "{}")
        .unwrap_err();
    assert!(matches!(err, LlmError::Connect(_)), "{err:?}");
}
