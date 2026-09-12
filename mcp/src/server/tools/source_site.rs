//! `source_site`: scrape a page and download the media matching the filters.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use serde::Serialize;

use super::{err_result, ok_result};
use crate::args::ScrapeParams;
use crate::outcome::ScrapedFile;
use crate::pipeline;

/// The `source_site` structured payload: the destination directory, the scraped page's host,
/// and every downloaded file — `ScrapedFile`'s `Serialize` shapes each `{path,width,height}`.
#[derive(Serialize)]
struct ScrapePayload<'a> {
    dir: Option<&'a str>,
    host: Option<&'a str>,
    files: &'a [ScrapedFile],
}

/// The tool's whole body: guard the surface, run the scrape, then report every file.
pub async fn run(params: ScrapeParams) -> Result<CallToolResult, McpError> {
        // Scraping only writes files locally: reject any other surface before the CLI runs.
        if let Err(message) = params.validate_surface() {
            return Ok(err_result(message));
        }

        let result = match pipeline::run_scrape(&params).await {
            Ok(result) => result,
            Err(error) => return Ok(err_result(error.to_string())),
        };

        // A human-readable summary: one line per file, then the count/host/dir tail.
        use std::fmt::Write;
        let host = result.host.as_deref().unwrap_or("the page");
        let mut summary = String::new();
        for file in &result.files {
            match (file.width, file.height) {
                (Some(w), Some(h)) => {
                    let _ = writeln!(summary, "wrote {} ({w}x{h} px)", file.path);
                }
                _ => {
                    let _ = writeln!(summary, "wrote {}", file.path);
                }
            }
        }
        let _ = write!(summary, "scraped {} file(s) from {host}", result.files.len());
        if let Some(dir) = &result.dir {
            let _ = write!(summary, " into {dir}");
        }

        let payload = ScrapePayload {
            dir: result.dir.as_deref(),
            host: result.host.as_deref(),
            files: &result.files,
        };
        ok_result(summary, payload)
}

#[cfg(test)]
mod tests {
    //! The scrape payload shape (pure) — the contract with a calling agent.

    use super::*;
    use serde_json::json;

    #[test]
    fn scrape_payload_shape_and_nulls() {
        let files = vec![
            ScrapedFile {
                path: "a.png".into(),
                width: Some(4),
                height: Some(8),
            },
            // Video has no measured dimensions.
            ScrapedFile {
                path: "b.mp4".into(),
                width: None,
                height: None,
            },
        ];
        let value = serde_json::to_value(ScrapePayload {
            dir: Some("/tmp/scrape"),
            host: Some("example.com"),
            files: &files,
        })
        .unwrap();

        assert_eq!(value["dir"], "/tmp/scrape");
        assert_eq!(value["host"], "example.com");
        assert_eq!(value["files"][0], json!({"path":"a.png","width":4,"height":8}));
        assert_eq!(value["files"][1], json!({"path":"b.mp4","width":null,"height":null}));

        // A scrape whose summary line was absent reports nulls, not missing keys.
        let bare = serde_json::to_value(ScrapePayload {
            dir: None,
            host: None,
            files: &[],
        })
        .unwrap();
        assert_eq!(bare, json!({"dir":null,"host":null,"files":[]}));
    }
}
