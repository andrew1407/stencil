//! The hand-rolled plain-http transport, tested against a canned `std::net::TcpListener`
//! on an ephemeral port — no external network, no CLI binary.

use std::io::{Read, Write};
use std::net::TcpListener;
use std::sync::mpsc;
use std::time::Duration;

use stencil_mcp::llmtransport::{
    guard_credentials, parse_http_url, validate_request_parts, HttpTarget, LlmError,
    LlmTransport, PlainHttpTransport,
};

/// Spawn a one-shot HTTP server: accept one connection, read the full request (headers +
/// `Content-Length` body), send `response` verbatim, close. The raw request bytes arrive
/// on the returned channel.
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

#[test]
fn parses_http_urls() {
    assert_eq!(
        parse_http_url("http://localhost:11434/api/chat").unwrap(),
        HttpTarget {
            host: "localhost".into(),
            port: 11434,
            path: "/api/chat".into()
        }
    );
    // Default port + default path.
    assert_eq!(
        parse_http_url("http://example.com").unwrap(),
        HttpTarget {
            host: "example.com".into(),
            port: 80,
            path: "/".into()
        }
    );
    // Query rides with the path.
    assert_eq!(
        parse_http_url("http://h:1/v1/chat?x=1").unwrap().path,
        "/v1/chat?x=1"
    );
    // Bracketed IPv6.
    assert_eq!(
        parse_http_url("http://[::1]:8090/llm/chat").unwrap(),
        HttpTarget {
            host: "::1".into(),
            port: 8090,
            path: "/llm/chat".into()
        }
    );
    // Bad ports / empty host.
    assert!(matches!(
        parse_http_url("http://host:notaport/x"),
        Err(LlmError::InvalidUrl(_))
    ));
    assert!(matches!(
        parse_http_url("http:///x"),
        Err(LlmError::InvalidUrl(_))
    ));
}

// ── Request guards ──

#[test]
fn crlf_in_a_header_or_path_is_refused_before_the_request_is_built() {
    let target = parse_http_url("http://localhost:11434/api/chat").unwrap();

    // A secret that picked up a stray newline must not forge a header line.
    let smuggled = vec![(
        "Authorization".to_string(),
        "Bearer k\r\nX-Injected: yes".to_string(),
    )];
    let err = validate_request_parts(&target, &smuggled).unwrap_err();
    assert!(matches!(err, LlmError::UnsafeHeader(_)), "{err}");
    assert!(err.to_string().contains("Authorization"), "{err}");

    let bad_name = vec![("X-A\r\nX-B".to_string(), "v".to_string())];
    assert!(matches!(
        validate_request_parts(&target, &bad_name).unwrap_err(),
        LlmError::UnsafeHeader(_)
    ));

    let nul = vec![("X-A".to_string(), "v\0w".to_string())];
    assert!(matches!(
        validate_request_parts(&target, &nul).unwrap_err(),
        LlmError::UnsafeHeader(_)
    ));

    let mut bad_path = target.clone();
    bad_path.path = "/api/chat\r\nX-Injected: yes".to_string();
    assert!(matches!(
        validate_request_parts(&bad_path, &[]).unwrap_err(),
        LlmError::UnsafeHeader(_)
    ));

    // The ordinary case still passes.
    let ok = vec![("Authorization".to_string(), "Bearer sk-local".to_string())];
    assert!(validate_request_parts(&target, &ok).is_ok());
}

#[test]
fn credentials_only_travel_to_loopback() {
    let creds = vec![("Authorization".to_string(), "Bearer sk-local".to_string())];
    let api_key = vec![("x-api-key".to_string(), "sk-ant".to_string())];
    let plain = vec![("Content-Type".to_string(), "application/json".to_string())];

    // This transport has no TLS, so an off-box peer would get the secret in cleartext.
    let err = guard_credentials(&creds, "llm.example.com", false).unwrap_err();
    assert!(matches!(err, LlmError::CredentialOffLoopback { .. }), "{err}");
    assert!(err.to_string().contains("loopback"), "{err}");
    assert!(guard_credentials(&api_key, "llm.example.com", false).is_err());

    // Loopback is the supported deployment; non-credential headers are always fine.
    assert!(guard_credentials(&creds, "localhost", true).is_ok());
    assert!(guard_credentials(&plain, "llm.example.com", false).is_ok());

    // An empty credential is not a credential.
    let empty = vec![("Authorization".to_string(), String::new())];
    assert!(guard_credentials(&empty, "llm.example.com", false).is_ok());
}
