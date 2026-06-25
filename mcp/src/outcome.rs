//! Parse the CLI's human-readable stderr into structured results.
//!
//! The CLI writes everything — banner, usage, errors, and the success line — to **stderr**
//! (stdout stays empty; the result is a written file). On success it prints exactly one
//! line `wrote {path} ({w}x{h})`. On failure it prints one or more `error: …` lines. We run
//! the child with `NO_COLOR=1` so this text is free of ANSI escapes.

/// A parsed success line: the resolved output path (extension auto-filled) and final size.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Wrote {
    pub path: String,
    pub width: u32,
    pub height: u32,
}

/// Find and parse the `wrote {path} ({w}x{h})` line, if present.
pub fn parse_wrote(stderr: &str) -> Option<Wrote> {
    for line in stderr.lines() {
        let line = line.trim();
        let Some(rest) = line.strip_prefix("wrote ") else {
            continue;
        };
        // Split off the trailing " (WxH)" — rfind so paths containing " (" still work.
        let Some(open) = rest.rfind(" (") else {
            continue;
        };
        let path = rest[..open].to_string();
        let Some(dims) = rest[open + 2..].strip_suffix(')') else {
            continue;
        };
        let Some((w, h)) = dims.split_once('x') else {
            continue;
        };
        if let (Ok(width), Ok(height)) = (w.trim().parse(), h.trim().parse()) {
            return Some(Wrote {
                path,
                width,
                height,
            });
        }
    }
    None
}

/// Pull the `error: …` line(s) out of stderr for surfacing back to the caller. Falls back
/// to the whole trimmed stderr when no `error:` prefix is found (e.g. unexpected output).
pub fn extract_errors(stderr: &str) -> String {
    let errors: Vec<&str> = stderr
        .lines()
        .map(|l| l.trim())
        .filter(|l| l.starts_with("error:"))
        .collect();

    if errors.is_empty() {
        let trimmed = stderr.trim();
        if trimmed.is_empty() {
            "the stencil CLI failed without a message".to_string()
        } else {
            trimmed.to_string()
        }
    } else {
        errors.join("\n")
    }
}
