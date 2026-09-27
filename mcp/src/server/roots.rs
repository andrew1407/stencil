//! The roots a call's writes are fenced into: the client's `roots/list` when it offers one,
//! else the operator's `STENCIL_MCP_ROOTS`, else the working directory (`Config::roots`).

use std::path::PathBuf;
use std::time::Duration;

use rmcp::{Peer, RoleServer};

use crate::confine::Roots;

/// A client that offers roots but never answers must not stall every call.
const ROOTS_TIMEOUT: Duration = Duration::from_secs(5);

/// The roots for one call, asked of the client afresh so a changed workspace is honoured.
pub async fn resolve(peer: &Peer<RoleServer>, fallback: &Roots) -> Roots {
    let offers = peer.peer_info().is_some_and(|info| info.capabilities.roots.is_some());
    if !offers {
        return fallback.clone();
    }
    #[allow(deprecated)]
    let listed = tokio::time::timeout(ROOTS_TIMEOUT, peer.list_roots()).await;
    let dirs: Vec<PathBuf> = match listed {
        Ok(Ok(result)) => result.roots.iter().filter_map(|root| file_path(&root.uri)).collect(),
        _ => Vec::new(),
    };
    Roots::new(dirs).unwrap_or_else(|| fallback.clone())
}

/// The local directory a `file://` root names; any other scheme is not a root to write in.
pub fn file_path(uri: &str) -> Option<PathBuf> {
    let rest = uri.strip_prefix("file://")?;
    let path = rest.strip_prefix("localhost").unwrap_or(rest);
    if !path.starts_with('/') {
        return None;
    }
    let decoded = percent_decode(path)?;
    // `file:///C:/work` names `C:/work` on Windows.
    let bytes = decoded.as_bytes();
    let drive = bytes.len() > 2 && bytes[2] == b':' && bytes[1].is_ascii_alphabetic();
    Some(PathBuf::from(if cfg!(windows) && drive { &decoded[1..] } else { &decoded[..] }))
}

/// `%XX` escapes decoded as UTF-8 bytes; a malformed escape or invalid UTF-8 is no path.
fn percent_decode(text: &str) -> Option<String> {
    let bytes = text.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' {
            let hex = std::str::from_utf8(bytes.get(i + 1..i + 3)?).ok()?;
            out.push(u8::from_str_radix(hex, 16).ok()?);
            i += 3;
        } else {
            out.push(bytes[i]);
            i += 1;
        }
    }
    String::from_utf8(out).ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_file_uri_becomes_its_decoded_path() {
        assert_eq!(file_path("file:///work/my%20shots"), Some(PathBuf::from("/work/my shots")));
        assert_eq!(file_path("file://localhost/srv"), Some(PathBuf::from("/srv")));
    }

    #[test]
    fn other_schemes_and_broken_escapes_are_no_root() {
        assert_eq!(file_path("https://example.com/work"), None);
        assert_eq!(file_path("file://host/share"), None);
        assert_eq!(file_path("file:///bad%zz"), None);
    }
}
