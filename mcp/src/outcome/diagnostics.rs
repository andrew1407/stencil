//! `--script-check`'s stdout (`cli/CONTRACT.md` §4.1): one
//! `<file>:<line>:<col>: error|warning: <message> [<CODE>]` line per diagnostic.

use schemars::JsonSchema;
use serde::Serialize;

/// One diagnostic, the §4.1 line split into fields; `code` is the stc-contract §8 code.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, JsonSchema)]
pub struct Diagnostic {
    pub severity: String,
    pub line: u32,
    pub col: u32,
    pub code: String,
    pub message: String,
}

/// Every diagnostic line, in source order; any other line is ignored.
pub fn parse_diagnostics(stdout: &str) -> Vec<Diagnostic> {
    stdout.lines().filter_map(|line| parse_line(line.trim())).collect()
}

/// Split from the right: a file name may itself hold `:` (a Windows drive).
fn parse_line(line: &str) -> Option<Diagnostic> {
    let (at, severity, rest) = ["error", "warning"].iter().find_map(|sev| {
        let marker = format!(": {sev}: ");
        line.find(&marker).map(|i| (&line[..i], *sev, &line[i + marker.len()..]))
    })?;
    let mut place = at.rsplitn(3, ':');
    let col = place.next()?.parse().ok()?;
    let line_no = place.next()?.parse().ok()?;
    place.next()?;
    let (message, code) = match rest.rfind(" [") {
        Some(open) if rest.ends_with(']') => (&rest[..open], &rest[open + 2..rest.len() - 1]),
        _ => (rest, ""),
    };
    Some(Diagnostic {
        severity: severity.to_string(),
        line: line_no,
        col,
        code: code.to_string(),
        message: message.to_string(),
    })
}
