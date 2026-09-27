//! Deliver a finished edit to the selected surface(s).
//!
//! The CLI has already written the output file; this module only presents the result: the
//! desktop app, a browser-editor launch URL, or a hand-off note. Each surface yields a
//! `DeliveryNote` rather than failing, so one unavailable surface never sinks the others.

mod launch;

use std::process::Stdio;

use serde::Serialize;

use crate::config::{Config, Surface};
use crate::pipeline::{gui_env, EditResult};

use launch::{build_launch_url, open_in_os, ECHO_MAX, OPEN_MAX};

/// One surface's outcome; its `Serialize` IS the payload's per-delivery object.
#[derive(Debug, Clone, Serialize, schemars::JsonSchema)]
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
                 has the chrome-devtools tools)",
            )
            .await,
            Surface::Extension => handoff_note(
                "extension",
                result,
                config,
                "page scanning/marking is driven by the stencil-operator agent + the Chrome \
                 extension",
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
    // None of our stdio is lent to it: stdout is the JSON-RPC channel.
    match tokio::process::Command::new(bin)
        .arg("--src")
        .arg(&result.path)
        .env_clear()
        .envs(gui_env(std::env::vars_os()))
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
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

/// Build a browser-editor launch URL with the result loaded, and optionally open it. An
/// opened URL is not echoed back; one too long for the result is not returned at all.
async fn deliver_browser(result: &EditResult, config: &Config) -> DeliveryNote {
    if result.width.is_none() {
        return DeliveryNote::fail("browser", PROJECT_NOT_IMAGE.into());
    }
    let cap = if config.auto_open { OPEN_MAX } else { ECHO_MAX };
    let url = match build_launch_url(&result.path, &config.browser_url, cap).await {
        Ok(url) => url,
        Err(e) => return DeliveryNote::fail("browser", e),
    };
    if !config.auto_open {
        let detail = format!("editor launch URL ready ({} app)", config.browser_url);
        return DeliveryNote::ok("browser", detail, Some(url));
    }
    match open_in_os(&url) {
        Ok(()) => DeliveryNote::ok(
            "browser",
            format!("opened in the editor at {}", config.browser_url),
            None,
        ),
        Err(e) => DeliveryNote::ok(
            "browser",
            format!("built the URL but could not auto-open it: {e}"),
            Some(url).filter(|u| u.len() <= ECHO_MAX),
        ),
    }
}

/// A hand-off note for the live/scan surfaces: the file to load, plus a launch URL when a
/// small result fits one.
async fn handoff_note(
    surface: &'static str,
    result: &EditResult,
    config: &Config,
    detail: &str,
) -> DeliveryNote {
    let url = match result.width {
        Some(_) => build_launch_url(&result.path, &config.browser_url, ECHO_MAX).await.ok(),
        None => None,
    };
    DeliveryNote::ok(surface, format!("{detail}; the file to load is {}", result.path), url)
}

const PROJECT_NOT_IMAGE: &str =
    "a .stencil project is not an image — the launch URL carries images only; open the \
     project in the desktop app or the editor's Open dialog";
