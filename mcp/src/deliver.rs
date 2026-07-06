//! Deliver a finished edit to the selected surface(s).
//!
//! The CLI has already done the pixel work and written the output file; this module just
//! presents that result somewhere: launches the desktop app, builds/opens a browser-editor
//! launch URL, or (for the live/scan surfaces) returns a hand-off note pointing at the
//! stencil-operator agent. Each surface yields a `DeliveryNote` instead of failing the whole
//! call, so one unavailable surface never sinks the others.

use std::path::Path;

use base64::Engine;
use serde::Serialize;

use crate::config::{Config, Surface};
use crate::pipeline::EditResult;

/// The outcome of delivering to one surface. `Serialize` produces the tool payload's
/// per-delivery object (`{"surface":…,"ok":…,"detail":…,"url":…}`) directly.
#[derive(Debug, Clone, Serialize)]
pub struct DeliveryNote {
    pub surface: &'static str,
    pub ok: bool,
    pub detail: String,
    /// A browser-editor launch URL, when one was produced.
    pub url: Option<String>,
}

impl DeliveryNote {
    fn ok(surface: &'static str, detail: String, url: Option<String>) -> Self {
        Self {
            surface,
            ok: true,
            detail,
            url,
        }
    }

    fn fail(surface: &'static str, detail: String) -> Self {
        Self {
            surface,
            ok: false,
            detail,
            url: None,
        }
    }
}

/// Deliver `result` to each surface, in order. The output file already has the crop,
/// rotation, layout, and filter baked in by the CLI, so every surface just opens that file.
pub async fn deliver(
    surfaces: &[Surface],
    result: &EditResult,
    config: &Config,
) -> Vec<DeliveryNote> {
    let mut notes = Vec::new();
    for &surface in surfaces {
        let note = match surface {
            Surface::Cli => DeliveryNote::ok("cli", format!("wrote {}", result.path), None),
            Surface::Desktop => deliver_desktop(result, config),
            Surface::Browser => deliver_browser(result, config),
            Surface::BrowserLive => handoff_note(
                "browser-live",
                result,
                config,
                "live editing of a running tab is driven by the stencil-operator agent (it \
                 has the chrome-devtools tools); open this URL there or with the agent",
            ),
            Surface::Extension => handoff_note(
                "extension",
                result,
                config,
                "page scanning/marking is driven by the stencil-operator agent + the Chrome \
                 extension; this server delivers the edited file and a launch URL",
            ),
        };
        notes.push(note);
    }
    notes
}

/// Launch the Qt desktop app, seeded with the finished output file.
fn deliver_desktop(result: &EditResult, config: &Config) -> DeliveryNote {
    let Some(bin) = &config.desktop_path else {
        return DeliveryNote::fail(
            "desktop",
            "desktop binary not found — build desktop/ or set STENCIL_DESKTOP".into(),
        );
    };

    // Fire-and-forget: the GUI runs independently of this server.
    match std::process::Command::new(bin)
        .arg("--src")
        .arg(&result.path)
        .spawn()
    {
        Ok(_) => DeliveryNote::ok(
            "desktop",
            format!("launched {} showing {}", bin.display(), result.path),
            None,
        ),
        Err(e) => DeliveryNote::fail(
            "desktop",
            format!("could not launch desktop app ({}): {e}", bin.display()),
        ),
    }
}

/// Build a browser-editor launch URL with the result loaded, and optionally open it.
fn deliver_browser(result: &EditResult, config: &Config) -> DeliveryNote {
    let url = match build_launch_url(&result.path, &config.browser_url) {
        Ok(url) => url,
        Err(e) => {
            return DeliveryNote::fail("browser", format!("could not build a launch URL: {e}"))
        }
    };

    let mut detail = format!("editor launch URL ready ({} app)", config.browser_url);
    if config.auto_open {
        detail = match open_in_os(&url) {
            Ok(()) => format!("opened in the editor at {}", config.browser_url),
            Err(e) => format!("built the URL but could not auto-open it: {e}"),
        };
    }

    DeliveryNote::ok("browser", detail, Some(url))
}

/// A hand-off note for the live/scan surfaces: still produce the editor launch URL.
fn handoff_note(
    surface: &'static str,
    result: &EditResult,
    config: &Config,
    detail: &str,
) -> DeliveryNote {
    let url = build_launch_url(&result.path, &config.browser_url).ok();
    DeliveryNote::ok(surface, detail.to_string(), url)
}

/// Build `<browser_url>/#stencil=<encodeURIComponent(JSON)>` carrying the result as a data
/// URL, the same fragment the extension uses to hand images to the editor.
fn build_launch_url(output_path: &str, browser_url: &str) -> Result<String, String> {
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
    match nibble {
        0..=9 => (b'0' + nibble) as char,
        _ => (b'A' + (nibble - 10)) as char,
    }
}

/// Open a URL with the platform opener (`open` on macOS, `xdg-open` on Linux, `cmd /c start`
/// on Windows).
fn open_in_os(url: &str) -> Result<(), String> {
    #[cfg(target_os = "macos")]
    let (program, args): (&str, &[&str]) = ("open", &[url]);
    #[cfg(target_os = "windows")]
    let (program, args): (&str, &[&str]) = ("cmd", &["/c", "start", "", url]);
    #[cfg(not(any(target_os = "macos", target_os = "windows")))]
    let (program, args): (&str, &[&str]) = ("xdg-open", &[url]);

    std::process::Command::new(program)
        .args(args)
        .spawn()
        .map(|_| ())
        .map_err(|e| e.to_string())
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
