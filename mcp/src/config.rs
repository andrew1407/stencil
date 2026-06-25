//! Server configuration: which delivery surface(s) a `stencil_edit` result goes to, plus
//! the paths/URLs those surfaces need.
//!
//! Resolution order, lowest precedence first:
//!   built-in defaults  <  a `.env` file  <  process env (incl. the mcpServers "env")  <
//!   the `--surface` CLI arg
//! and a per-call `surface` tool parameter overrides the resolved default for one call.
//!
//! The `.env` loader is a tiny hand-rolled `KEY=VALUE` reader — no extra dependency — that
//! only sets a variable when the process env doesn't already define it (so real env wins).

use std::path::PathBuf;

/// Where a finished edit is delivered. The CLI always does the pixel work; surfaces beyond
/// `Cli` present the result somewhere.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Surface {
    /// Write the output file (always implied).
    Cli,
    /// Launch the Qt desktop app showing the result.
    Desktop,
    /// Build (and optionally open) a browser-editor launch URL with the result loaded.
    Browser,
    /// Live-drive a running browser editor — delegated to the stencil-operator agent.
    BrowserLive,
    /// Scan/mark page images — delegated to the stencil-operator agent.
    Extension,
}

impl Surface {
    /// Parse one surface token (case-insensitive; `-`/`_`/spaces are equivalent).
    pub fn parse(token: &str) -> Result<Surface, String> {
        match token.trim().to_ascii_lowercase().replace(['_', ' '], "-").as_str() {
            "cli" => Ok(Surface::Cli),
            "desktop" => Ok(Surface::Desktop),
            "browser" => Ok(Surface::Browser),
            "browser-live" | "browserlive" | "live" => Ok(Surface::BrowserLive),
            "extension" => Ok(Surface::Extension),
            other => Err(format!(
                "unknown surface '{other}' (expected: cli, desktop, browser, browser-live, extension)"
            )),
        }
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Surface::Cli => "cli",
            Surface::Desktop => "desktop",
            Surface::Browser => "browser",
            Surface::BrowserLive => "browser-live",
            Surface::Extension => "extension",
        }
    }
}

/// Parse a comma/space-separated surface list, de-duplicated, order preserved. Always keeps
/// `Cli` present (the file write is the basis every other surface builds on). Tolerant of a
/// JSON-array-looking string (`["cli","browser"]`) so the env var accepts the same shape the
/// per-call `surface` parameter does.
pub fn parse_surfaces(spec: &str) -> Result<Vec<Surface>, String> {
    let spec = spec.trim().trim_start_matches('[').trim_end_matches(']');
    let mut out: Vec<Surface> = Vec::new();
    for raw in spec.split([',', ' ']) {
        let token = raw.trim().trim_matches(['"', '\'']).trim();
        if token.is_empty() {
            continue;
        }
        let surface = Surface::parse(token)?;
        if !out.contains(&surface) {
            out.push(surface);
        }
    }
    if !out.contains(&Surface::Cli) {
        out.insert(0, Surface::Cli);
    }
    Ok(out)
}

/// Resolved server configuration.
#[derive(Debug, Clone)]
pub struct Config {
    /// Default delivery surfaces when a tool call doesn't specify its own.
    pub default_surfaces: Vec<Surface>,
    /// Qt desktop binary path (for the `desktop` surface).
    pub desktop_path: Option<PathBuf>,
    /// Base URL of the served browser editor (for the `browser` surface).
    pub browser_url: String,
    /// Auto-open the browser launch URL with the OS opener.
    pub auto_open: bool,
}

const DEFAULT_BROWSER_URL: &str = "http://localhost:8080";

impl Default for Config {
    fn default() -> Self {
        Config {
            default_surfaces: vec![Surface::Cli],
            desktop_path: None,
            browser_url: DEFAULT_BROWSER_URL.to_string(),
            auto_open: false,
        }
    }
}

impl Config {
    /// Build the config from a `.env` file, the process environment, and `--surface <list>`
    /// in `args`. Returns the config and any non-fatal warnings (e.g. a bad surface token,
    /// which is ignored in favour of the default).
    pub fn load(args: &[String]) -> (Config, Vec<String>) {
        load_dotenv();

        let mut config = Config::default();
        let mut warnings: Vec<String> = Vec::new();

        // Surfaces: process env, then the --surface arg (arg wins).
        let surface_spec =
            arg_value(args, "--surface").or_else(|| std::env::var("STENCIL_SURFACES").ok());
        if let Some(spec) = surface_spec {
            match parse_surfaces(&spec) {
                Ok(surfaces) => config.default_surfaces = surfaces,
                Err(e) => warnings.push(format!("ignoring STENCIL_SURFACES/--surface: {e}")),
            }
        }

        if let Ok(path) = std::env::var("STENCIL_DESKTOP") {
            if !path.trim().is_empty() {
                config.desktop_path = Some(PathBuf::from(path));
            }
        }
        if config.desktop_path.is_none() {
            config.desktop_path = default_desktop_path();
        }

        if let Ok(url) = std::env::var("STENCIL_BROWSER_URL") {
            if !url.trim().is_empty() {
                config.browser_url = url.trim().trim_end_matches('/').to_string();
            }
        }

        if let Ok(flag) = std::env::var("STENCIL_AUTO_OPEN") {
            config.auto_open = matches!(
                flag.trim().to_ascii_lowercase().as_str(),
                "1" | "true" | "yes" | "on"
            );
        }

        (config, warnings)
    }
}

/// The default desktop binary location inside a repo checkout.
fn default_desktop_path() -> Option<PathBuf> {
    let candidate = crate::locate::repo_root()?.join("desktop/build/stencil_gui");
    candidate.is_file().then_some(candidate)
}

/// Read `--flag value` out of an argv slice.
fn arg_value(args: &[String], flag: &str) -> Option<String> {
    let pos = args.iter().position(|a| a == flag)?;
    args.get(pos + 1).cloned()
}

/// Minimal `.env` loader: read `KEY=VALUE` lines from the first `.env` found near the CWD
/// or the executable, setting each variable only if the process env hasn't already set it.
fn load_dotenv() {
    let Some(path) = dotenv_path() else {
        return;
    };
    let Ok(contents) = std::fs::read_to_string(&path) else {
        return;
    };
    for line in contents.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let line = line.strip_prefix("export ").unwrap_or(line);
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        let key = key.trim();
        if key.is_empty() || std::env::var_os(key).is_some() {
            continue;
        }
        // Strip matching surrounding quotes, if any.
        let value = value.trim();
        let value = value
            .strip_prefix('"')
            .and_then(|v| v.strip_suffix('"'))
            .or_else(|| value.strip_prefix('\'').and_then(|v| v.strip_suffix('\'')))
            .unwrap_or(value);
        std::env::set_var(key, value);
    }
}

/// Locate a `.env` file: the CWD first, then beside the running executable.
fn dotenv_path() -> Option<PathBuf> {
    let mut candidates: Vec<PathBuf> = Vec::new();
    if let Ok(cwd) = std::env::current_dir() {
        candidates.push(cwd.join(".env"));
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            candidates.push(dir.join(".env"));
        }
    }
    candidates.into_iter().find(|p| p.is_file())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_known_surface_tokens() {
        assert_eq!(Surface::parse("cli"), Ok(Surface::Cli));
        assert_eq!(Surface::parse(" Desktop "), Ok(Surface::Desktop));
        assert_eq!(Surface::parse("BROWSER"), Ok(Surface::Browser));
        assert_eq!(Surface::parse("browser_live"), Ok(Surface::BrowserLive));
        assert_eq!(Surface::parse("browser-live"), Ok(Surface::BrowserLive));
        assert!(Surface::parse("bogus").is_err());
    }

    #[test]
    fn surface_list_dedupes_and_always_keeps_cli() {
        assert_eq!(
            parse_surfaces("desktop, browser, desktop").unwrap(),
            vec![Surface::Cli, Surface::Desktop, Surface::Browser]
        );
        // cli is prepended when not named.
        assert_eq!(
            parse_surfaces("browser").unwrap(),
            vec![Surface::Cli, Surface::Browser]
        );
        // an explicit cli keeps its position.
        assert_eq!(
            parse_surfaces("cli desktop").unwrap(),
            vec![Surface::Cli, Surface::Desktop]
        );
    }

    #[test]
    fn surface_list_tolerates_json_array_string() {
        // A user typing the array form into the env string still works.
        assert_eq!(
            parse_surfaces(r#"["cli","browser"]"#).unwrap(),
            vec![Surface::Cli, Surface::Browser]
        );
        assert_eq!(
            parse_surfaces("['desktop', 'browser']").unwrap(),
            vec![Surface::Cli, Surface::Desktop, Surface::Browser]
        );
    }

    #[test]
    fn surface_list_rejects_a_bad_token() {
        assert!(parse_surfaces("browser, nope").is_err());
    }

    #[test]
    fn arg_value_reads_a_following_token() {
        let args = vec!["bin".into(), "--surface".into(), "cli,browser".into()];
        assert_eq!(
            arg_value(&args, "--surface").as_deref(),
            Some("cli,browser")
        );
        assert_eq!(arg_value(&args, "--missing"), None);
    }
}
