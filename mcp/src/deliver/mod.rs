//! Deliver a finished edit to the selected surface(s).
//!
//! The CLI has already written the output file; this module only presents the result: the
//! desktop app, a browser-editor launch URL, or a hand-off note. Each surface yields a
//! `DeliveryNote` rather than failing, so one unavailable surface never sinks the others.

mod launch;

use serde::Serialize;

use crate::config::{Config, Surface};
use crate::pipeline::EditResult;

use launch::{build_launch_url, open_in_os};

/// One surface's outcome; its `Serialize` IS the payload's per-delivery object.
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
        Self { surface, ok: true, detail, url }
    }

    fn fail(surface: &'static str, detail: String) -> Self {
        Self { surface, ok: false, detail, url: None }
    }
}

/// Deliver `result` to each surface, in order. The CLI baked the crop, rotation, layout and
/// filter into the output file, so every surface just opens that file.
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
            Surface::Browser => deliver_browser(result, config).await,
            Surface::BrowserLive => handoff_note(
                "browser-live",
                result,
                config,
                "live editing of a running tab is driven by the stencil-operator agent (it \
                 has the chrome-devtools tools); open this URL there or with the agent",
            )
            .await,
            Surface::Extension => handoff_note(
                "extension",
                result,
                config,
                "page scanning/marking is driven by the stencil-operator agent + the Chrome \
                 extension; this server delivers the edited file and a launch URL",
            )
            .await,
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

    // Fire-and-forget: the GUI runs on its own; `tokio::process` reaps the child, std zombies.
    match tokio::process::Command::new(bin)
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
async fn deliver_browser(result: &EditResult, config: &Config) -> DeliveryNote {
    let url = match build_launch_url(&result.path, &config.browser_url).await {
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
async fn handoff_note(
    surface: &'static str,
    result: &EditResult,
    config: &Config,
    detail: &str,
) -> DeliveryNote {
    let url = build_launch_url(&result.path, &config.browser_url).await.ok();
    DeliveryNote::ok(surface, detail.to_string(), url)
}
