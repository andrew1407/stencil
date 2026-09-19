//! A hand-rolled, dependency-free HTTP/1.1 POST transport for the LLM providers.
//!
//! Per `llm-contract.md` every client uses its platform's built-in networking and no new
//! dependency; here that is `std::net::TcpStream`, plain `http://` only — an `https://`
//! URL is rejected. The trait is synchronous: async callers use `spawn_blocking`.

use std::sync::LazyLock;
use std::time::Duration;

mod client;
mod guards;
mod response;
mod sanitize;
mod url;

pub use client::PlainHttpTransport;
pub use guards::{guard_credentials, validate_request_parts};
pub use sanitize::{error_code, error_reason, sanitize_detail};
pub(crate) use sanitize::{clip, SNIPPET_LEN};
pub use url::{parse_http_url, HttpTarget};

/// Connect + read/write timeout applied to every request: the contract's
/// `timeouts.chatSeconds`, parsed from the embedded canonical providers.json.
pub static DEFAULT_TIMEOUT: LazyLock<Duration> = LazyLock::new(|| {
    let asset: serde_json::Value =
        serde_json::from_str(include_str!("../../../browser/js/config/llm/providers.json"))
            .expect("canonical browser/js/config/llm/providers.json is not valid JSON");
    let secs = asset["timeouts"]["chatSeconds"]
        .as_u64()
        .expect("providers.json: timeouts.chatSeconds must be a number");
    Duration::from_secs(secs)
});

/// Response guards so a misbehaving endpoint can't balloon memory. The body cap matches the
/// collaboration server's `maxResponseBytes` (`server/internal/llm/anthropic.go`, 8 MiB).
const MAX_HEADER_BYTES: usize = 64 * 1024;
const MAX_BODY_BYTES: usize = 8 * 1024 * 1024;
const BODY_TOO_LARGE: &str = "response body exceeds 8 MiB";

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
    /// The endpoint answered HTTP with a non-2xx status. `reason` is the provider's own
    /// message (sanitized, bounded); `code` is the body's machine `code`, empty when absent.
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
