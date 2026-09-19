//! Request guards — what must hold before a socket opens.

use super::{HttpTarget, LlmError};

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

/// Refuse to put a credential on the wire off-loopback: this transport has no TLS.
/// `peer_is_loopback` comes from the address connected to, not the spelling of the host.
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
