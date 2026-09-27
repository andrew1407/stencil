//! The transport's one overall deadline — a reply that drips in slowly is cut off, not read
//! forever — its abort, which ends an exchange before that deadline, and a redirect, which is
//! reported and never followed.

use std::io::{Read, Write};
use std::net::TcpListener;
use std::sync::{mpsc, Arc};
use std::time::{Duration, Instant};

use stencil_mcp::llmtransport::{LlmError, LlmTransport, PlainHttpTransport};

/// Accept one connection, hand back its request head, then send `head` and drip `drip`
/// one byte per `pace`.
fn server(
    head: &'static str,
    drip: &'static [u8],
    pace: Duration,
) -> (String, mpsc::Receiver<String>) {
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind");
    let addr = listener.local_addr().unwrap();
    let (sender, receiver) = mpsc::channel();
    std::thread::spawn(move || {
        let (mut stream, _) = listener.accept().expect("accept");
        let mut request = Vec::new();
        let mut block = [0u8; 4096];
        while !request.windows(4).any(|w| w == b"\r\n\r\n") {
            match stream.read(&mut block) {
                Ok(0) | Err(_) => break,
                Ok(n) => request.extend_from_slice(&block[..n]),
            }
        }
        let _ = sender.send(String::from_utf8_lossy(&request).into_owned());
        let _ = stream.write_all(head.as_bytes());
        for byte in drip {
            std::thread::sleep(pace);
            if stream.write_all(&[*byte]).is_err() {
                return;
            }
        }
    });
    (format!("http://{addr}"), receiver)
}

/// Every single read beats the per-read timeout, yet the whole reply would take seconds:
/// the overall deadline is what ends it.
#[test]
fn a_slow_drip_is_cut_off_at_the_overall_deadline() {
    let (base, _) = server(
        "HTTP/1.1 200 OK\r\nContent-Length: 40\r\n\r\n",
        b"0123456789012345678901234567890123456789",
        Duration::from_millis(150),
    );
    let started = Instant::now();
    let error = PlainHttpTransport::with_timeout(Duration::from_millis(900))
        .post_json(&format!("{base}/slow"), &[], "{}")
        .expect_err("the drip outlasts the deadline");

    assert!(started.elapsed() < Duration::from_secs(3), "took {:?}", started.elapsed());
    assert!(matches!(error, LlmError::Io(_)), "got {error:?}");
}

#[test]
fn a_redirect_is_reported_never_followed() {
    let (base, _) = server(
        "HTTP/1.1 302 Found\r\nLocation: http://elsewhere.test/\r\nContent-Length: 0\r\n\r\n",
        b"",
        Duration::ZERO,
    );
    let error = PlainHttpTransport::with_timeout(Duration::from_secs(5))
        .post_json(&format!("{base}/api/chat"), &[], "{}")
        .expect_err("a 3xx is not a success");
    assert!(matches!(error, LlmError::Status { status: 302, .. }), "got {error:?}");
}

/// An endpoint that takes the request and never answers: an abort is what ends the wait,
/// long before the deadline, and every later request is refused.
#[test]
fn an_abort_ends_the_exchange_in_flight_and_refuses_the_next() {
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind");
    let base = format!("http://{}", listener.local_addr().unwrap());
    let (arrived, request_seen) = mpsc::channel();
    let accepting = listener.try_clone().unwrap();
    std::thread::spawn(move || {
        let (mut stream, _) = accepting.accept().expect("accept");
        let mut block = [0u8; 4096];
        let _ = stream.read(&mut block);
        let _ = arrived.send(());
        while matches!(stream.read(&mut block), Ok(n) if n > 0) {}
    });
    let transport = Arc::new(PlainHttpTransport::with_timeout(Duration::from_secs(60)));
    let (worker, url) = (transport.clone(), format!("{base}/api/chat"));
    let exchange = std::thread::spawn(move || worker.post_json(&url, &[], "{}"));

    request_seen.recv_timeout(Duration::from_secs(20)).expect("the request arrived");
    let aborted_at = Instant::now();
    transport.abort();
    assert!(exchange.join().unwrap().is_err(), "an aborted exchange is no reply");
    assert!(aborted_at.elapsed() < Duration::from_secs(2), "took {:?}", aborted_at.elapsed());

    let refused = transport.post_json(&format!("{base}/api/chat"), &[], "{}").unwrap_err();
    assert!(refused.to_string().contains("cancelled"), "got {refused:?}");
    drop(listener);
}
