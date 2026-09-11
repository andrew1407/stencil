//! The real transport: connect, write one request, hand the reply to [`super::response`].

use std::io::Write;
use std::net::{TcpStream, ToSocketAddrs};
use std::time::Duration;

use super::response::read_response;
use super::sanitize::{error_code, error_reason};
use super::{guard_credentials, parse_http_url, validate_request_parts};
use super::{LlmError, LlmTransport, DEFAULT_TIMEOUT};

/// The plain-http transport: TCP connect with a timeout, one `HTTP/1.1` request with
/// `Connection: close`, and a response reader that handles both `Content-Length` and
/// `chunked` bodies (falling back to read-to-EOF when neither is declared).
pub struct PlainHttpTransport {
    timeout: Duration,
}

impl Default for PlainHttpTransport {
    fn default() -> Self {
        Self::new()
    }
}

impl PlainHttpTransport {
    pub fn new() -> Self {
        Self {
            timeout: *DEFAULT_TIMEOUT,
        }
    }

    pub fn with_timeout(timeout: Duration) -> Self {
        Self { timeout }
    }
}

impl LlmTransport for PlainHttpTransport {
    fn post_json(
        &self,
        url: &str,
        headers: &[(String, String)],
        body: &str,
    ) -> Result<String, LlmError> {
        let target = parse_http_url(url)?;
        validate_request_parts(&target, headers)?;

        let addr = (target.host.as_str(), target.port)
            .to_socket_addrs()
            .map_err(|e| LlmError::Connect(format!("could not resolve {}: {e}", target.host)))?
            .next()
            .ok_or_else(|| {
                LlmError::Connect(format!("no address found for host {}", target.host))
            })?;
        // Before the socket opens — nothing leaves the machine when this fails.
        guard_credentials(headers, &target.host, addr.ip().is_loopback())?;
        let mut stream = TcpStream::connect_timeout(&addr, self.timeout).map_err(|e| {
            LlmError::Connect(format!(
                "could not connect to {}:{}: {e}",
                target.host, target.port
            ))
        })?;
        stream
            .set_read_timeout(Some(self.timeout))
            .and_then(|_| stream.set_write_timeout(Some(self.timeout)))
            .map_err(|e| LlmError::Io(format!("could not set socket timeouts: {e}")))?;

        // Host: bracket IPv6 literals; omit the default port.
        let host_part = if target.host.contains(':') {
            format!("[{}]", target.host)
        } else {
            target.host.clone()
        };
        let host_header = if target.port == 80 {
            host_part
        } else {
            format!("{host_part}:{}", target.port)
        };

        let mut request = format!(
            "POST {} HTTP/1.1\r\nHost: {}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n",
            target.path,
            host_header,
            body.len()
        );
        for (name, value) in headers {
            request.push_str(name);
            request.push_str(": ");
            request.push_str(value);
            request.push_str("\r\n");
        }
        request.push_str("\r\n");

        stream
            .write_all(request.as_bytes())
            .and_then(|_| stream.write_all(body.as_bytes()))
            .map_err(|e| LlmError::Io(format!("could not send the request: {e}")))?;

        let (status, text) = read_response(&mut stream)?;
        if (200..300).contains(&status) {
            Ok(text)
        } else {
            Err(LlmError::Status {
                status,
                reason: error_reason(&text),
                code: error_code(&text),
            })
        }
    }
}
