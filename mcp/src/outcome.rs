//! Parse the CLI's human-readable stderr into structured results.
//!
//! The CLI writes everything to stderr: on success exactly one `wrote {path} ({w}x{h} px ·
//! {page})` line (older builds a bare `({w}x{h})`), on failure one or more `error: …`
//! lines. The child runs with `NO_COLOR=1`, so this text carries no ANSI escapes.

use serde::Serialize;

mod diagnostics;
mod scrape;

pub use diagnostics::{parse_diagnostics, Diagnostic};
pub use scrape::{parse_scraped, Scraped, ScrapedFile};

// ── CLI output line prefixes ──
const PREFIX_WROTE: &str = "wrote ";
const PREFIX_UPDATED: &str = "updated server result for project ";
const PREFIX_CREATED: &str = "created server project ";
const PREFIX_ERROR: &str = "error:";
const PREFIX_NOTE: &str = "note:";

/// A parsed success line: the resolved output path (extension auto-filled) and final size.
/// `Serialize` produces the per-file object a script run's payload carries.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, schemars::JsonSchema)]
pub struct Wrote {
    pub path: String,
    pub width: u32,
    pub height: u32,
}

/// Find and parse the first `wrote {path} ({w}x{h} …)` line, if present.
pub fn parse_wrote(stderr: &str) -> Option<Wrote> {
    wrote_lines(stderr).next()
}

/// Every `wrote …` line, in order — a script run prints one per `@save`.
pub fn parse_all_wrote(stderr: &str) -> Vec<Wrote> {
    wrote_lines(stderr).collect()
}

/// Lazy so the single-line caller stops at the first match.
fn wrote_lines(stderr: &str) -> impl Iterator<Item = Wrote> + '_ {
    stderr.lines().filter_map(|line| {
        let (path, tail) = split_wrote(line)?;
        let (width, height) = parse_dims(first_token(tail.strip_suffix(')')?))?;
        Some(Wrote { path, width, height })
    })
}

/// A `wrote …` line's path and the text after its LAST " (" — so a path containing " (" survives.
pub(crate) fn split_wrote(line: &str) -> Option<(String, &str)> {
    let rest = line.trim().strip_prefix(PREFIX_WROTE)?;
    match rest.rfind(" (") {
        Some(open) => Some((rest[..open].to_string(), &rest[open + 2..])),
        None => Some((rest.to_string(), "")),
    }
}

/// The CLI's `note: …` lines with the prefix stripped — what a run says without failing
/// ("no files matched", "the script saved nothing").
pub fn parse_notes(stderr: &str) -> Vec<String> {
    let notes = stderr.lines().filter_map(|l| l.trim().strip_prefix(PREFIX_NOTE));
    notes.map(|rest| rest.trim().to_string()).collect()
}

/// Find the `wrote {path} (project)` line a `.stencil` write prints (§2.1 `save`). A project
/// is a document, so the CLI reports no dimensions — which is why `parse_wrote` skips it.
pub fn parse_wrote_project(stderr: &str) -> Option<String> {
    parse_documents(stderr).into_iter().find(|(_, kind)| kind == "project").map(|(p, _)| p)
}

/// Every `wrote {path} ({kind})` line whose tail is a word, not a size: `project`, and the
/// `python` / `javascript` a `--script-emit` prints.
pub fn parse_documents(stderr: &str) -> Vec<(String, String)> {
    let lines = stderr.lines().filter_map(|line| {
        let (path, tail) = split_wrote(line)?;
        let kind = tail.strip_suffix(')')?;
        let word = !kind.is_empty() && kind.bytes().all(|b| b.is_ascii_lowercase());
        word.then(|| (path, kind.to_string()))
    });
    lines.collect()
}

/// A server-side delivery the CLI performed after the local write: the result written back
/// into a fetched project, or pushed as a new one. `Serialize` is the wire object.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, schemars::JsonSchema)]
#[serde(tag = "action", rename_all = "lowercase")]
pub enum Remote {
    /// From `--remote-update`: `updated server result for project {id} ({w}x{h})`.
    Updated { id: String, width: u32, height: u32 },
    /// From `--remote`: `created server project "{name}" ({id})`.
    Created { name: String, id: String },
}

impl Remote {
    /// The one-line human-readable summary the MCP server prints for this delivery
    /// (without a leading newline).
    pub fn summary_line(&self) -> String {
        match self {
            Remote::Updated { id, width, height } => {
                format!("↑ server: updated project {id} ({width}x{height})")
            }
            Remote::Created { name, id } => {
                format!("↑ server: created project \"{name}\" ({id})")
            }
        }
    }
}

/// Parse any collaboration-server delivery line(s) the CLI prints. One call can both update
/// a fetched project and create a new one, so all are returned, in order.
pub fn parse_remotes(stderr: &str) -> Vec<Remote> {
    let mut out = Vec::new();
    for line in stderr.lines() {
        let line = line.trim();
        if let Some(rest) = line.strip_prefix(PREFIX_UPDATED) {
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
        } else if let Some(rest) = line.strip_prefix(PREFIX_CREATED) {
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

/// The leading token of a parenthetical tail; newer builds append " px · {page}" after it.
pub(crate) fn first_token(tail: &str) -> &str {
    tail.split_whitespace().next().unwrap_or("")
}

/// Parse a leading `WxH` token (`^\d+x\d+`) into dimensions, else `None`.
pub(crate) fn parse_dims(token: &str) -> Option<(u32, u32)> {
    let (w, h) = token.split_once('x')?;
    Some((w.parse().ok()?, h.parse().ok()?))
}

/// Pull the `error: …` line(s) out of stderr for surfacing back to the caller. Falls back
/// to the whole trimmed stderr when no `error:` prefix is found (e.g. unexpected output).
pub fn extract_errors(stderr: &str) -> String {
    let errors: Vec<&str> = stderr
        .lines()
        .map(|l| l.trim())
        .filter(|l| l.starts_with(PREFIX_ERROR))
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
