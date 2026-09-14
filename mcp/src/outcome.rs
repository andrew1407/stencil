//! Parse the CLI's human-readable stderr into structured results.
//!
//! The CLI writes everything — banner, usage, errors, and the success line — to **stderr**
//! (stdout stays empty; the result is a written file). On success it prints exactly one
//! line `wrote {path} ({w}x{h} px · {page})` (the page-format suffix is informational; older
//! builds printed a bare `({w}x{h})`). On failure it prints one or more `error: …` lines. We run
//! the child with `NO_COLOR=1` so this text is free of ANSI escapes.

use serde::Serialize;

// ── CLI output line prefixes ──
// The exact stderr line prefixes the CLI (`cli/`) emits and this module parses. Centralized so
// the output contract is single-sourced and greppable; the parsers below match on these
// instead of bare literals.
const PREFIX_WROTE: &str = "wrote ";
const PREFIX_UPDATED: &str = "updated server result for project ";
const PREFIX_CREATED: &str = "created server project ";
const PREFIX_SCRAPED: &str = "scraped ";
const PREFIX_ERROR: &str = "error:";
const PREFIX_NOTE: &str = "note:";

/// A parsed success line: the resolved output path (extension auto-filled) and final size.
/// `Serialize` produces the per-file object a script run's payload carries.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
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
fn split_wrote(line: &str) -> Option<(String, &str)> {
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

/// Find the `wrote {path} (project)` line a `.stencil` bundle write prints (contract §2.1's
/// `save`), returning its path. A project is a document, not pixels, so the CLI reports no
/// dimensions here — which is exactly why `parse_wrote` skips this line.
pub fn parse_wrote_project(stderr: &str) -> Option<String> {
    for line in stderr.lines() {
        if let Some(rest) = line.trim().strip_prefix(PREFIX_WROTE) {
            if let Some(path) = rest.strip_suffix(" (project)") {
                return Some(path.to_string());
            }
        }
    }
    None
}

/// A server-side delivery the CLI performed after writing the local file: the result was
/// either written back into a fetched project, or pushed as a brand-new project.
///
/// `Serialize` produces the tool payload's per-delivery object directly
/// (`{"action":"updated",…}` / `{"action":"created",…}`).
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
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

/// Parse any collaboration-server delivery line(s) the CLI prints after a successful write.
/// A single call can both update a fetched project and create a new one, so this returns all
/// it finds, in order. Pure — no network.
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

/// One downloaded media file from a scrape run: its written path and — for image-category
/// items whose bytes the CLI measured — the pixel dimensions. Video (and any unmeasured)
/// items carry `None`. `Serialize` produces the tool payload's per-file object directly.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct ScrapedFile {
    pub path: String,
    pub width: Option<u32>,
    pub height: Option<u32>,
}

/// A parsed successful scrape: the files written plus the optional summary's host/directory.
/// Pure — the caller pairs it with the exit status and `extract_errors` for the failure path,
/// exactly like `parse_wrote` (a zero-file success is treated as an error upstream).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Scraped {
    pub dir: Option<String>,
    pub host: Option<String>,
    pub files: Vec<ScrapedFile>,
}

/// Parse the scrape mode's stderr per §3 of the design contract's multi-file rule: every
/// `wrote …` line is one file, its dims `None` where the tail is not a `WxH` (video lines),
/// and `scraped {n} file(s) from {host} into {dir}` carries the summary's host + dir.
/// Error lines are left to `extract_errors`, as with the edit path. Pure — no network.
pub fn parse_scraped(stderr: &str) -> Scraped {
    let mut files = Vec::new();
    let mut host = None;
    let mut dir = None;
    for line in stderr.lines() {
        let line = line.trim();
        if let Some((path, tail)) = split_wrote(line) {
            let tail = tail.strip_suffix(')').unwrap_or(tail);
            let (width, height) = match parse_dims(first_token(tail)) {
                Some((w, h)) => (Some(w), Some(h)),
                None => (None, None),
            };
            files.push(ScrapedFile { path, width, height });
        } else if let Some(rest) = line.strip_prefix(PREFIX_SCRAPED) {
            // "{n} file(s) from {host} into {dir}"
            if let Some((h, d)) = rest
                .split_once(" from ")
                .and_then(|(_, after)| after.split_once(" into "))
            {
                host = Some(h.to_string());
                dir = Some(d.to_string());
            }
        }
    }
    Scraped { dir, host, files }
}

/// The leading token of a parenthetical tail; newer builds append " px · {page}" after it.
fn first_token(tail: &str) -> &str {
    tail.split_whitespace().next().unwrap_or("")
}

/// Parse a leading `WxH` token (`^\d+x\d+`) into dimensions, else `None`.
fn parse_dims(token: &str) -> Option<(u32, u32)> {
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
