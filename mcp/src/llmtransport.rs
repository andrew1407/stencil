//! A hand-rolled, dependency-free HTTP/1.1 POST transport for the LLM providers.
//!
//! Per the design rules in `llm-contract.md`, every Stencil client talks to its LLM
//! endpoint with the platform's built-in networking and **no new dependency** — for this
//! crate that means a small hand-written HTTP/1.1 client over `std::net::TcpStream`,
//! deliberately limited to plain `http://` (local providers like Ollama / LM Studio, or an
//! `http://` collaboration server). TLS is out of scope; an `https://` URL is rejected with
//! a clear message instead of half-working.
//!
//! The trait is synchronous on purpose: the async tool in `server.rs` calls it through
//! `tokio::task::spawn_blocking`, which keeps `std::net` + OS socket timeouts (simple,
//! deterministic) and avoids widening the tokio feature list in `Cargo.toml`.

use std::io::{Read, Write};
use std::net::{TcpStream, ToSocketAddrs};
use std::sync::LazyLock;
use std::time::Duration;

/// Connect + read/write timeout applied to every request: the contract's
/// `timeouts.chatSeconds`, parsed from the embedded canonical providers.json.
pub static DEFAULT_TIMEOUT: LazyLock<Duration> = LazyLock::new(|| {
    let asset: serde_json::Value =
        serde_json::from_str(include_str!("../../browser/js/config/llm/providers.json"))
            .expect("canonical browser/js/config/llm/providers.json is not valid JSON");
    let secs = asset["timeouts"]["chatSeconds"]
        .as_u64()
        .expect("providers.json: timeouts.chatSeconds must be a number");
    Duration::from_secs(secs)
});

/// Response guards so a misbehaving endpoint can't balloon memory.
const MAX_HEADER_BYTES: usize = 64 * 1024;
const MAX_BODY_BYTES: usize = 64 * 1024 * 1024;
const BODY_TOO_LARGE: &str = "response body exceeds 64 MiB";

// ── Errors ──

/// Everything a transport `post_json` can fail with. Hand-written (no `thiserror`), like
/// `args::EditError`; `Display` is the exact user-facing message.
#[derive(Debug)]
pub enum LlmError {
    /// The URL scheme is not `http://` (https, ftp, a bare host, …).
    UnsupportedScheme(String),
    /// The URL had an empty/unparseable host or port.
    InvalidUrl(String),
    /// DNS resolution or TCP connect failed (includes the connect timeout).
    Connect(String),
    /// A socket read/write failed mid-request (includes read/write timeouts).
    Io(String),
    /// The endpoint answered with something that isn't parseable HTTP.
    BadResponse(String),
    /// The endpoint answered HTTP, but with a non-2xx status. `reason` is the provider's
    /// own message (sanitized, bounded), empty when the body carried none; `code` is the
    /// body's machine `code` ("llmDisabled", "llmUpstream", …), empty when absent.
    Status { status: u16, reason: String, code: String },
    /// A header name/value or the path carried CR, LF or NUL.
    UnsafeHeader(String),
    /// A credential header was about to be sent to a non-loopback host over cleartext.
    CredentialOffLoopback { header: String, host: String },
}

impl std::fmt::Display for LlmError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            LlmError::UnsupportedScheme(url) => write!(
                f,
                "cannot request '{url}': mcp's built-in transport is plain-http; use a \
                 local provider or an http:// server"
            ),
            LlmError::InvalidUrl(url) => write!(f, "invalid http URL '{url}'"),
            LlmError::Connect(detail) => f.write_str(detail),
            LlmError::Io(detail) => f.write_str(detail),
            LlmError::BadResponse(detail) => write!(f, "malformed HTTP response: {detail}"),
            // The reason ONCE (contract §6.3): the provider's message when it has one,
            // else the bare status — never both, and never the raw body.
            LlmError::Status { status, reason, .. } if reason.is_empty() => {
                write!(f, "the LLM endpoint answered HTTP {status}")
            }
            LlmError::Status { reason, .. } => f.write_str(reason),
            LlmError::UnsafeHeader(what) => write!(
                f,
                "refusing to send the request: {what} contains a line break or NUL"
            ),
            LlmError::CredentialOffLoopback { header, host } => write!(
                f,
                "refusing to send '{header}' to {host}: mcp's built-in transport is \
                 plain-http (no TLS), so credentials are only sent to loopback. Point \
                 STENCIL_LLM_BASE_URL / _SERVER_URL at a local endpoint or a local TLS \
                 proxy."
            ),
        }
    }
}

impl std::error::Error for LlmError {}

// ── The transport trait ──

/// One JSON `POST`. Implemented by [`PlainHttpTransport`] for real requests and by mocks in
/// the test suite (which record the URL/headers/body and return canned replies).
pub trait LlmTransport: Send + Sync {
    /// POST `body` (JSON) to `url` with the extra `headers`, returning the response body on
    /// a 2xx status.
    fn post_json(
        &self,
        url: &str,
        headers: &[(String, String)],
        body: &str,
    ) -> Result<String, LlmError>;
}

// ── URL parsing ──

/// A parsed plain-http target.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HttpTarget {
    pub host: String,
    /// Default `80`.
    pub port: u16,
    /// Path + query, always starting with `/`.
    pub path: String,
}

/// Parse an `http://host[:port][/path]` URL. Any other scheme — notably `https://` — is
/// rejected with the plain-http message. Bracketed IPv6 hosts are supported; userinfo is not.
pub fn parse_http_url(url: &str) -> Result<HttpTarget, LlmError> {
    let Some(rest) = url.strip_prefix("http://") else {
        return Err(LlmError::UnsupportedScheme(url.to_string()));
    };
    let (authority, path) = match rest.find('/') {
        Some(i) => (&rest[..i], &rest[i..]),
        None => (rest, "/"),
    };

    let (host, port_str) = if let Some(bracketed) = authority.strip_prefix('[') {
        // IPv6 literal: [::1] or [::1]:8090
        let Some(end) = bracketed.find(']') else {
            return Err(LlmError::InvalidUrl(url.to_string()));
        };
        let after = &bracketed[end + 1..];
        let port_str = match after.strip_prefix(':') {
            Some(p) => Some(p),
            None if after.is_empty() => None,
            None => return Err(LlmError::InvalidUrl(url.to_string())),
        };
        (&bracketed[..end], port_str)
    } else {
        match authority.rsplit_once(':') {
            Some((h, p)) => (h, Some(p)),
            None => (authority, None),
        }
    };

    if host.is_empty() {
        return Err(LlmError::InvalidUrl(url.to_string()));
    }
    let port: u16 = match port_str {
        None => 80,
        Some(p) => p
            .parse()
            .map_err(|_| LlmError::InvalidUrl(url.to_string()))?,
    };
    Ok(HttpTarget {
        host: host.to_string(),
        port,
        path: path.to_string(),
    })
}

// ── Request guards ──

/// Header names that carry a secret. Compared case-insensitively.
const CREDENTIAL_HEADERS: [&str; 2] = ["authorization", "x-api-key"];

/// True when a byte string would break out of its header line.
fn has_control_break(s: &str) -> bool {
    s.contains('\r') || s.contains('\n') || s.contains('\0')
}

/// Reject anything that would forge its own header line: the request is assembled by string
/// concatenation, so a `\r\n` in a name, value or path is a header-injection primitive.
pub fn validate_request_parts(target: &HttpTarget, headers: &[(String, String)]) -> Result<(), LlmError> {
    if has_control_break(&target.path) {
        return Err(LlmError::UnsafeHeader("the request path".to_string()));
    }
    for (name, value) in headers {
        if name.trim().is_empty() {
            return Err(LlmError::UnsafeHeader("an empty header name".to_string()));
        }
        if has_control_break(name) {
            return Err(LlmError::UnsafeHeader(format!("header name '{name}'")));
        }
        if has_control_break(value) {
            return Err(LlmError::UnsafeHeader(format!("the value of header '{name}'")));
        }
    }
    Ok(())
}

/// Refuse to put a credential on the wire off-loopback: this transport has no TLS, so an
/// off-box endpoint would receive the key in cleartext. `peer_is_loopback` comes from the
/// address actually connected to, not the spelling of the host.
pub fn guard_credentials(
    headers: &[(String, String)],
    host: &str,
    peer_is_loopback: bool,
) -> Result<(), LlmError> {
    if peer_is_loopback {
        return Ok(());
    }
    for (name, value) in headers {
        if value.is_empty() {
            continue;
        }
        let lower = name.to_ascii_lowercase();
        if CREDENTIAL_HEADERS.contains(&lower.as_str()) {
            return Err(LlmError::CredentialOffLoopback {
                header: name.clone(),
                host: host.to_string(),
            });
        }
    }
    Ok(())
}

// ── The real transport ──

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

// ── Response reading ──

/// Read one HTTP response off `stream`: status line + headers, then the body per its
/// framing (`Transfer-Encoding: chunked` > `Content-Length` > read-to-EOF).
fn read_response(stream: &mut TcpStream) -> Result<(u16, String), LlmError> {
    let mut buf: Vec<u8> = Vec::with_capacity(8 * 1024);

    // 1. Accumulate until the header terminator.
    let header_end = loop {
        if let Some(i) = find_subslice(&buf, b"\r\n\r\n") {
            break i;
        }
        if buf.len() > MAX_HEADER_BYTES {
            return Err(LlmError::BadResponse(
                "response headers exceed 64 KiB".to_string(),
            ));
        }
        if fill(stream, &mut buf)? == 0 {
            return Err(LlmError::BadResponse(
                "connection closed before the response headers completed".to_string(),
            ));
        }
    };

    // 2. Status line + the framing headers.
    let head = std::str::from_utf8(&buf[..header_end])
        .map_err(|_| LlmError::BadResponse("response headers are not UTF-8".to_string()))?;
    let mut lines = head.split("\r\n");
    let status_line = lines.next().unwrap_or("");
    let mut parts = status_line.split_whitespace();
    let proto = parts.next().unwrap_or("");
    if !proto.starts_with("HTTP/") {
        return Err(LlmError::BadResponse(format!(
            "not an HTTP status line: '{status_line}'"
        )));
    }
    let status: u16 = parts.next().and_then(|s| s.parse().ok()).ok_or_else(|| {
        LlmError::BadResponse(format!("no status code in '{status_line}'"))
    })?;

    let mut content_length: Option<usize> = None;
    let mut chunked = false;
    for line in lines {
        let Some((name, value)) = line.split_once(':') else {
            continue;
        };
        match name.trim().to_ascii_lowercase().as_str() {
            "content-length" => content_length = value.trim().parse().ok(),
            "transfer-encoding" => {
                chunked = value.trim().to_ascii_lowercase().contains("chunked");
            }
            _ => {}
        }
    }

    // 3. Body — whatever already arrived past the terminator, plus the rest of the stream.
    let rest = buf.split_off(header_end + 4);
    let body = if chunked {
        read_chunked(rest, stream)?
    } else if let Some(len) = content_length {
        read_exact_len(rest, stream, len)?
    } else {
        read_to_eof(rest, stream)?
    };
    Ok((status, String::from_utf8_lossy(&body).into_owned()))
}

/// Decode a `Transfer-Encoding: chunked` body: hex-size line, chunk bytes, CRLF, repeated
/// until the `0` chunk (trailers are ignored). `buf` starts with whatever body bytes were
/// read alongside the headers; more is pulled from `stream` on demand.
fn read_chunked(mut buf: Vec<u8>, stream: &mut TcpStream) -> Result<Vec<u8>, LlmError> {
    let mut body: Vec<u8> = Vec::new();
    let mut pos = 0usize;
    loop {
        // The chunk-size line.
        let line_end = loop {
            if let Some(i) = find_subslice(&buf[pos..], b"\r\n") {
                break pos + i;
            }
            if fill(stream, &mut buf)? == 0 {
                return Err(LlmError::BadResponse(
                    "connection closed inside a chunked body".to_string(),
                ));
            }
        };
        let size_text = std::str::from_utf8(&buf[pos..line_end])
            .map_err(|_| LlmError::BadResponse("chunk size line is not UTF-8".to_string()))?;
        // Chunk extensions (";…") are allowed and ignored.
        let size_token = size_text.split(';').next().unwrap_or("").trim();
        let size = usize::from_str_radix(size_token, 16).map_err(|_| {
            LlmError::BadResponse(format!("invalid chunk size '{size_token}'"))
        })?;
        pos = line_end + 2;

        if size == 0 {
            return Ok(body); // done; any trailers are ignored
        }
        if body.len() + size > MAX_BODY_BYTES {
            return Err(LlmError::BadResponse(BODY_TOO_LARGE.to_string()));
        }
        // The chunk data + its trailing CRLF.
        while buf.len() < pos + size + 2 {
            if fill(stream, &mut buf)? == 0 {
                return Err(LlmError::BadResponse(
                    "connection closed inside a chunked body".to_string(),
                ));
            }
        }
        body.extend_from_slice(&buf[pos..pos + size]);
        // Drop the consumed bytes so `buf` never holds the whole raw stream alongside
        // `body` (which would double peak memory on large responses).
        buf.drain(..pos + size + 2);
        pos = 0;
    }
}

/// Read until `buf` holds exactly `len` body bytes (a declared `Content-Length`).
fn read_exact_len(
    mut buf: Vec<u8>,
    stream: &mut TcpStream,
    len: usize,
) -> Result<Vec<u8>, LlmError> {
    if len > MAX_BODY_BYTES {
        return Err(LlmError::BadResponse(BODY_TOO_LARGE.to_string()));
    }
    if buf.len() < len {
        let remaining = len - buf.len();
        buf.reserve(remaining);
        let read = Read::take(&mut *stream, remaining as u64)
            .read_to_end(&mut buf)
            .map_err(|e| LlmError::Io(format!("could not read the response: {e}")))?;
        if read < remaining {
            return Err(LlmError::BadResponse(format!(
                "connection closed after {} of {len} body bytes",
                buf.len()
            )));
        }
    }
    buf.truncate(len);
    Ok(buf)
}

/// No framing declared: `Connection: close` semantics — the body is everything until EOF.
fn read_to_eof(mut buf: Vec<u8>, stream: &mut TcpStream) -> Result<Vec<u8>, LlmError> {
    loop {
        if buf.len() > MAX_BODY_BYTES {
            return Err(LlmError::BadResponse(BODY_TOO_LARGE.to_string()));
        }
        if fill(stream, &mut buf)? == 0 {
            return Ok(buf);
        }
    }
}

/// One `read` into a scratch block appended to `buf`; returns the byte count (0 = EOF).
fn fill(stream: &mut TcpStream, buf: &mut Vec<u8>) -> Result<usize, LlmError> {
    let mut block = [0u8; 4096];
    let n = stream
        .read(&mut block)
        .map_err(|e| LlmError::Io(format!("could not read the response: {e}")))?;
    buf.extend_from_slice(&block[..n]);
    Ok(n)
}

/// First index of `needle` in `haystack`.
fn find_subslice(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    haystack
        .windows(needle.len())
        .position(|window| window == needle)
}

/// How much of a quoted body is included in an error message — a bad LLM reply in `llm`,
/// an invalid op-plan in `server`.
pub(crate) const SNIPPET_LEN: usize = 400;

// ── Non-2xx bodies ──

/// How much of a provider's own prose an error may quote (contract §6.3).
const DETAIL_LEN: usize = 200;

/// The provider's own message out of a non-2xx body — `{message}` (stencil-server),
/// `{"error":"…"}` (ollama) or `{"error":{"message":"…"}}` (openai-compat), the §6 shapes
/// every client parses — sanitized by [`sanitize_detail`]. Empty when the body carries
/// nothing usable: the raw body is NEVER shown to the user.
pub fn error_reason(body: &str) -> String {
    let Ok(value) = serde_json::from_str::<serde_json::Value>(body) else {
        return String::new();
    };
    let raw = value
        .get("message")
        .and_then(|v| v.as_str())
        .or_else(|| match value.get("error") {
            Some(serde_json::Value::String(s)) => Some(s.as_str()),
            Some(e) => e.get("message").and_then(|v| v.as_str()),
            None => None,
        });
    raw.map(sanitize_detail).unwrap_or_default()
}

/// The machine `code` out of a non-2xx body (the stencil-server error shape, e.g.
/// "llmDisabled") — "" when the body is not JSON or carries none.
pub fn error_code(body: &str) -> String {
    serde_json::from_str::<serde_json::Value>(body)
        .ok()
        .and_then(|v| v.get("code").and_then(|c| c.as_str()).map(str::to_string))
        .unwrap_or_default()
}

/// Untrusted provider prose made safe to quote: control characters out (a message must
/// not forge extra lines), URL- and token-shaped words redacted (an endpoint may echo the
/// key back), whitespace collapsed, cut at `DETAIL_LEN` on a word boundary.
pub fn sanitize_detail(text: &str) -> String {
    let head: String = text.chars().take(4 * DETAIL_LEN).collect();
    let words: Vec<&str> = head
        .split(|c: char| c.is_whitespace() || c.is_control())
        .filter(|w| !w.is_empty())
        .collect();
    let mut out = String::new();
    let mut len = 0; // characters written
    let mut i = 0;
    while i < words.len() {
        let raw = words[i];
        // `Bearer <token>` / `Basic <token>` collapse to ONE [redacted] (browser parity).
        let (word, used) = if is_auth_scheme(raw)
            && words.get(i + 1).is_some_and(|w| is_auth_param(w))
        {
            ("[redacted]", 2)
        } else if secretish(raw) {
            ("[redacted]", 1)
        } else {
            (raw, 1)
        };
        let sep = usize::from(!out.is_empty());
        let n = word.chars().count();
        if len + sep + n > DETAIL_LEN {
            out.push('…');
            return out;
        }
        if sep == 1 {
            out.push(' ');
        }
        out.push_str(word);
        len += sep + n;
        i += used;
    }
    out
}

/// An HTTP auth scheme whose following credential must never be echoed.
fn is_auth_scheme(word: &str) -> bool {
    word.eq_ignore_ascii_case("bearer") || word.eq_ignore_ascii_case("basic")
}

/// The credential after `Bearer`/`Basic`: 8+ chars of the token68-ish alphabet the
/// browser's `(?:bearer|basic) +[A-Za-z0-9._~+/=-]{8,}` rule accepts.
fn is_auth_param(word: &str) -> bool {
    word.chars().count() >= 8
        && word
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '~' | '+' | '/' | '=' | '-'))
}

/// True for a word that must not be echoed back: an absolute URL (an internal endpoint is
/// not the caller's business), a long opaque run, or a `key=`/`sk-`-style credential.
fn secretish(word: &str) -> bool {
    fn token_chars(s: &str) -> bool {
        s.chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-'))
    }
    if word.contains("://") || (word.len() >= 24 && token_chars(word)) {
        return true;
    }
    // Known credential heads (compound ones like api_key first, so `api_key=…` is not
    // mis-split at the `_` after `api`), then a separator and 6+ token chars.
    const HEADS: [&str; 8] =
        ["api_key", "api-key", "apikey", "sk", "pk", "key", "token", "secret"];
    let lower = word.to_ascii_lowercase();
    HEADS.iter().any(|head| {
        lower.strip_prefix(head).is_some_and(|rest| {
            rest.starts_with(['-', '_', '=', ':']) && rest.len() - 1 >= 6 && token_chars(&rest[1..])
        })
    })
}

/// Bound `text` to at most `max` bytes (cut on a char boundary, `…` appended) so it can be
/// quoted in an error message. Shared with `llm`'s bad-reply errors.
pub(crate) fn clip(text: &str, max: usize) -> String {
    let trimmed = text.trim();
    if trimmed.len() <= max {
        return trimmed.to_string();
    }
    let mut end = max;
    while !trimmed.is_char_boundary(end) {
        end -= 1;
    }
    format!("{}…", &trimmed[..end])
}
