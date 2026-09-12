//! The browser-editor launch URL and the platform opener.
//!
//! `<browser_url>/#stencil=<encodeURIComponent(JSON)>` is the same fragment the Chrome
//! extension uses to hand an image to the browser app, so the encoding must match
//! JavaScript's `encodeURIComponent` byte for byte.

use std::path::Path;

use base64::Engine;

/// Build `<browser_url>/#stencil=<encodeURIComponent(JSON)>` carrying the result as a data
/// URL — the fragment the extension uses to hand images to the editor.
pub(super) fn build_launch_url(output_path: &str, browser_url: &str) -> Result<String, String> {
    let bytes = std::fs::read(output_path).map_err(|e| format!("reading '{output_path}': {e}"))?;
    let mime = mime_for(output_path);
    let encoded = base64::engine::general_purpose::STANDARD.encode(&bytes);
    let data_url = format!("data:{mime};base64,{encoded}");

    let name = Path::new(output_path)
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_else(|| "stencil".to_string());

    // The browser's applyExternalLaunch() reads { dataUrl, name } from the fragment.
    let payload = serde_json::json!({ "dataUrl": data_url, "name": name });
    let json = serde_json::to_string(&payload).map_err(|e| e.to_string())?;
    Ok(format!(
        "{browser_url}/#stencil={}",
        encode_uri_component(&json)
    ))
}

/// Guess a MIME type from the output extension (the formats the CLI can write).
fn mime_for(path: &str) -> &'static str {
    match Path::new(path)
        .extension()
        .and_then(|e| e.to_str())
        .map(|e| e.to_ascii_lowercase())
        .as_deref()
    {
        Some("jpg") | Some("jpeg") => "image/jpeg",
        Some("bmp") => "image/bmp",
        Some("tga") => "image/x-tga",
        _ => "image/png",
    }
}

/// Percent-encode like JavaScript's `encodeURIComponent`: everything except
/// `A-Z a-z 0-9 - _ . ! ~ * ' ( )` is escaped as UTF-8 `%XX` bytes.
fn encode_uri_component(input: &str) -> String {
    const UNRESERVED: &[u8] = b"-_.!~*'()";
    let mut out = String::with_capacity(input.len());
    for &byte in input.as_bytes() {
        if byte.is_ascii_alphanumeric() || UNRESERVED.contains(&byte) {
            out.push(byte as char);
        } else {
            out.push('%');
            out.push(hex_digit(byte >> 4));
            out.push(hex_digit(byte & 0x0f));
        }
    }
    out
}

fn hex_digit(nibble: u8) -> char {
    (if nibble < 10 { b'0' + nibble } else { b'A' + nibble - 10 }) as char
}

/// The platform opener and its argv. Split out because the spawn half cannot be tested
/// without launching a browser — this half can.
fn opener_argv(url: &str) -> (&'static str, Vec<&str>) {
    #[cfg(target_os = "macos")]
    return ("open", vec![url]);
    // The empty argument is `start`'s window title; without it a quoted URL becomes one.
    #[cfg(target_os = "windows")]
    return ("cmd", vec!["/c", "start", "", url]);
    #[cfg(not(any(target_os = "macos", target_os = "windows")))]
    return ("xdg-open", vec![url]);
}

/// Open a URL with the platform opener.
pub(super) fn open_in_os(url: &str) -> Result<(), String> {
    let (program, args) = opener_argv(url);
    std::process::Command::new(program).args(args).spawn().map(|_| ()).map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    #[test]
    fn encode_uri_component_matches_javascript() {
        // Unreserved characters pass through; everything else is %XX (UTF-8).
        assert_eq!(encode_uri_component("aZ0-_.!~*'()"), "aZ0-_.!~*'()");
        assert_eq!(encode_uri_component("a b/c?d=e&f"), "a%20b%2Fc%3Fd%3De%26f");
        assert_eq!(encode_uri_component("{\"x\":1}"), "%7B%22x%22%3A1%7D");
        assert_eq!(encode_uri_component("é"), "%C3%A9");
    }

    #[test]
    fn mime_for_known_extensions() {
        assert_eq!(mime_for("a.png"), "image/png");
        assert_eq!(mime_for("a.JPG"), "image/jpeg");
        assert_eq!(mime_for("a.jpeg"), "image/jpeg");
        assert_eq!(mime_for("a.bmp"), "image/bmp");
        assert_eq!(mime_for("a.unknown"), "image/png");
    }

    /// The half of `open_in_os` that can be asserted: the opener and its argv, with the URL
    /// always last.
    #[test]
    fn opener_argv_names_the_platform_opener_with_the_url_last() {
        let url = "http://localhost:8080/#stencil=%7B%7D";
        let (program, args) = opener_argv(url);
        let expected: (&str, Vec<&str>) = match (cfg!(target_os = "macos"), cfg!(target_os = "windows")) {
            (true, _) => ("open", vec![url]),
            (_, true) => ("cmd", vec!["/c", "start", "", url]),
            _ => ("xdg-open", vec![url]),
        };
        assert_eq!((program, args), expected);
    }

    #[test]
    fn build_launch_url_carries_a_data_url_fragment() {
        let mut file = tempfile::Builder::new().suffix(".png").tempfile().unwrap();
        file.write_all(b"\x89PNG\r\n").unwrap();
        let path = file.path().to_string_lossy().into_owned();

        let url = build_launch_url(&path, "http://localhost:8080").unwrap();
        assert!(url.starts_with("http://localhost:8080/#stencil="));
        // The fragment is percent-encoded JSON embedding a PNG data URL.
        assert!(url.contains("data%3Aimage%2Fpng%3Bbase64%2C"));
    }
}
