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

/// A server-side delivery the CLI performed after writing the local file: the result was
/// either written back into a fetched project, or pushed as a brand-new project.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Remote {
    /// From `--remote-update`: `updated server result for project {id} ({w}x{h})`.
    Updated { id: String, width: u32, height: u32 },
    /// From `--remote`: `created server project "{name}" ({id})`.
    Created { name: String, id: String },
}

/// Parse any collaboration-server delivery line(s) the CLI prints after a successful write.
/// A single call can both update a fetched project and create a new one, so this returns all
/// it finds, in order. Pure — no network.
pub fn parse_remotes(stderr: &str) -> Vec<Remote> {
    let mut out = Vec::new();
    for line in stderr.lines() {
        let line = line.trim();
        if let Some(rest) = line.strip_prefix("updated server result for project ") {
            // `{id} ({w}x{h})` — rfind " (" so an id can't be confused with the dims.
            let Some(open) = rest.rfind(" (") else {
                continue;
            };
            let id = rest[..open].to_string();
            let Some(dims) = rest[open + 2..].strip_suffix(')') else {
                continue;
            };
            let Some((w, h)) = dims.split_once('x') else {
                continue;
            };
            if let (Ok(width), Ok(height)) = (w.trim().parse(), h.trim().parse()) {
                out.push(Remote::Updated { id, width, height });
            }
        } else if let Some(rest) = line.strip_prefix("created server project ") {
            // `"{name}" ({id})` — the id is the parenthesised tail; the name is quoted.
            let Some(open) = rest.rfind(" (") else {
                continue;
            };
            let Some(id) = rest[open + 2..].strip_suffix(')') else {
                continue;
            };
            let name = rest[..open].trim().trim_matches('"').to_string();
            out.push(Remote::Created {
                name,
                id: id.to_string(),
            });
        }
    }
    out
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
