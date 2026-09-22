//! Where a finished edit is delivered, and how a surface list is spelled: the CLI always
//! does the pixel work, so it is always in the list whether or not the caller named it.

/// Where a finished edit is delivered. The CLI always does the pixel work; other surfaces
/// present the result somewhere.
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

/// Parse a comma/space-separated surface list, de-duplicated, order preserved. `Cli` is
/// always kept — the file write is the basis every other surface builds on.
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
}
