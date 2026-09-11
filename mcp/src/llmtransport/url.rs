//! URL parsing: plain `http://` only, host + port + path.

use super::LlmError;

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
