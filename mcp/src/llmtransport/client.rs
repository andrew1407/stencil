//! The real transport: resolve, connect, write one request, hand the reply to
//! [`super::response`] — all inside one overall deadline, so a slow drip is cut off too.

use std::io::Write;
use std::net::{Shutdown, SocketAddr, TcpStream, ToSocketAddrs};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{mpsc, Mutex, PoisonError};
use std::time::{Duration, Instant};

use super::response::read_response;
use super::sanitize::{error_code, error_reason};
use super::{guard_credentials, parse_http_url, validate_request_parts, HttpTarget};
use super::{LlmError, LlmTransport, DEFAULT_TIMEOUT, MAX_BODY_BYTES};

/// The plain-http transport: one `HTTP/1.1` request with `Connection: close`, and a reader
/// for `Content-Length`, `chunked` or read-to-EOF bodies. A redirect is never followed.
pub struct PlainHttpTransport {
    timeout: Duration,
    aborted: AtomicBool,
    /// A handle on the socket in flight, which `abort` shuts to wake its blocked read.
    live: Mutex<Option<TcpStream>>,
}

impl Default for PlainHttpTransport {
    fn default() -> Self {
        Self::new()
    }
}

impl PlainHttpTransport {
    pub fn new() -> Self {
        Self::with_timeout(*DEFAULT_TIMEOUT)
    }

    /// `timeout` bounds the whole exchange: DNS, connect, write and every read.
    pub fn with_timeout(timeout: Duration) -> Self {
        Self { timeout, aborted: AtomicBool::new(false), live: Mutex::new(None) }
    }

    /// Hand `abort` a handle on `stream` until the guard drops; refused once aborted, which
    /// `abort` sets before it looks for the handle, so neither order misses the other.
    fn track(&self, stream: &TcpStream) -> Result<Tracked<'_>, LlmError> {
        let handle = stream
            .try_clone()
            .map_err(|e| LlmError::Io(format!("could not set up the request: {e}")))?;
        let tracked = Tracked(&self.live);
        *self.live.lock().unwrap_or_else(PoisonError::into_inner) = Some(handle);
        match self.aborted.load(Ordering::SeqCst) {
            true => Err(LlmError::Io("the request was cancelled".to_string())),
            false => Ok(tracked),
        }
    }

    fn exchange(
        &self,
        method: &str,
        url: &str,
        headers: &[(String, String)],
        body: Option<&str>,
    ) -> Result<String, LlmError> {
        let deadline = Instant::now() + self.timeout;
        let target = parse_http_url(url)?;
        validate_request_parts(&target, headers)?;
        let mut stream = connect(&target, headers, deadline)?;
        let _tracked = self.track(&stream)?;
        stream
            .set_write_timeout(Some(remaining(deadline)?))
            .map_err(|e| LlmError::Io(format!("could not set socket timeouts: {e}")))?;

        // Host: bracket IPv6 literals; omit the default port.
        let host_part = match target.host.contains(':') {
            true => format!("[{}]", target.host),
            false => target.host.clone(),
        };
        let host_header = match target.port {
            80 => host_part,
            port => format!("{host_part}:{port}"),
        };
        let mut request = format!("{method} {} HTTP/1.1\r\nHost: {host_header}\r\n", target.path);
        if let Some(body) = body {
            request.push_str("Content-Type: application/json\r\n");
            request.push_str(&format!("Content-Length: {}\r\n", body.len()));
        }
        request.push_str("Connection: close\r\n");
        for (name, value) in headers {
            request.push_str(&format!("{name}: {value}\r\n"));
        }
        request.push_str("\r\n");

        stream
            .write_all(request.as_bytes())
            .and_then(|_| stream.write_all(body.unwrap_or_default().as_bytes()))
            .map_err(|e| LlmError::Io(format!("could not send the request: {e}")))?;

        let (status, text) = read_response(&mut stream, deadline, MAX_BODY_BYTES)?;
        match (200..300).contains(&status) {
            true => Ok(text),
            false => Err(LlmError::Status {
                status,
                reason: error_reason(&text),
                code: error_code(&text),
            }),
        }
    }
}

impl LlmTransport for PlainHttpTransport {
    fn post_json(
        &self,
        url: &str,
        headers: &[(String, String)],
        body: &str,
    ) -> Result<String, LlmError> {
        self.exchange("POST", url, headers, Some(body))
    }

    fn abort(&self) {
        self.aborted.store(true, Ordering::SeqCst);
        if let Some(stream) = self.live.lock().unwrap_or_else(PoisonError::into_inner).take() {
            let _ = stream.shutdown(Shutdown::Both);
        }
    }
}

/// Clears the in-flight handle when its exchange ends.
struct Tracked<'a>(&'a Mutex<Option<TcpStream>>);

impl Drop for Tracked<'_> {
    fn drop(&mut self) {
        self.0.lock().unwrap_or_else(PoisonError::into_inner).take();
    }
}

/// Try every resolved address in turn. The credential guard runs per address, before its
/// socket opens — nothing leaves the machine for an address that fails it.
fn connect(
    target: &HttpTarget,
    headers: &[(String, String)],
    deadline: Instant,
) -> Result<TcpStream, LlmError> {
    let (host, port) = (target.host.as_str(), target.port);
    let mut last = LlmError::Connect(format!("no address found for host {host}"));
    for addr in resolve(host, port, deadline)? {
        if let Err(refused) = guard_credentials(headers, host, addr.ip().is_loopback()) {
            last = refused;
            continue;
        }
        match TcpStream::connect_timeout(&addr, remaining(deadline)?) {
            Ok(stream) => return Ok(stream),
            Err(e) => last = LlmError::Connect(format!("could not connect to {host}:{port}: {e}")),
        }
    }
    Err(last)
}

/// DNS on a helper thread, since `to_socket_addrs` has no timeout of its own; a lookup that
/// outlives the deadline is abandoned, not waited for.
fn resolve(host: &str, port: u16, deadline: Instant) -> Result<Vec<SocketAddr>, LlmError> {
    let (sender, receiver) = mpsc::channel();
    let name = host.to_string();
    std::thread::spawn(move || {
        let found = (name.as_str(), port).to_socket_addrs().map(|a| a.collect::<Vec<_>>());
        let _ = sender.send(found);
    });
    match receiver.recv_timeout(remaining(deadline)?) {
        Ok(Ok(addrs)) => Ok(addrs),
        Ok(Err(e)) => Err(LlmError::Connect(format!("could not resolve {host}: {e}"))),
        Err(_) => Err(LlmError::Connect(format!("resolving {host} timed out"))),
    }
}

/// What is left of the exchange's budget; spent is a timeout.
pub(super) fn remaining(deadline: Instant) -> Result<Duration, LlmError> {
    let left = deadline.saturating_duration_since(Instant::now());
    match left.is_zero() {
        true => Err(LlmError::Io("the request timed out".to_string())),
        false => Ok(left),
    }
}
