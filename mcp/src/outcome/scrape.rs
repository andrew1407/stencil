//! The scrape mode's stderr (`cli/CONTRACT.md` §3): one `wrote` line per downloaded file and
//! the `scraped … from {host} into {dir}` summary.

use serde::Serialize;

use super::{first_token, parse_dims, split_wrote};

const PREFIX_SCRAPED: &str = "scraped ";

/// One downloaded media file from a scrape run: its path and, for measured images, the
/// pixel dimensions. Video and unmeasured items carry `None`.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, schemars::JsonSchema)]
pub struct ScrapedFile {
    pub path: String,
    pub width: Option<u32>,
    pub height: Option<u32>,
}

/// A parsed successful scrape: the files written plus the summary's host/directory. Pure —
/// the caller pairs it with the exit status and `extract_errors`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Scraped {
    pub dir: Option<String>,
    pub host: Option<String>,
    pub files: Vec<ScrapedFile>,
}

/// Parse the scrape mode's stderr: every `wrote …` line is one file (dims `None` where the
/// tail is not `WxH`), and `scraped {n} file(s) from {host} into {dir}` the summary.
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
