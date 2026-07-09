//! Orchestration: turn typed parameters into a CLI run and a structured result.
//!
//! Mirrors the role of `cli/src/pipeline.zig` on the wrapper side — it locates the binary,
//! materializes an inline layout, spawns the CLI (with `NO_COLOR=1`), and maps the exit
//! status + stderr into a result or an error. All pixel work happens in the CLI/core, so
//! output is identical to the browser, desktop, and CLI front-ends by construction.

use std::path::Path;

use crate::args::{self, EditError, EditParams, LayoutArg, ScrapeParams};
use crate::locate;
use crate::outcome::{self, ScrapedFile};

/// A successful edit: the resolved output path, the final image dimensions, and any
/// collaboration-server deliveries the CLI performed (project updated / created).
#[derive(Debug, Clone)]
pub struct EditResult {
    pub path: String,
    pub width: u32,
    pub height: u32,
    pub remotes: Vec<outcome::Remote>,
}

impl EditResult {
    /// The human-readable head of the tool summary: the local write line followed by one
    /// line per collaboration-server delivery. The delivery-surface lines are appended by
    /// the handler, which owns the surface notes.
    pub fn summary(&self) -> String {
        let mut summary = format!("wrote {} ({}x{})", self.path, self.width, self.height);
        for remote in &self.remotes {
            summary.push('\n');
            summary.push_str(&remote.summary_line());
        }
        summary
    }
}

/// A successful scrape: the destination directory and page host (from the CLI's summary
/// line, when present) plus every downloaded file. `files` reuses `outcome::ScrapedFile`, so
/// the server payload serializes straight off it.
#[derive(Debug, Clone)]
pub struct ScrapeResult {
    pub dir: Option<String>,
    pub host: Option<String>,
    pub files: Vec<ScrapedFile>,
}

/// Raw capture from one CLI invocation.
struct CliOutput {
    success: bool,
    stderr: String,
}

/// Run one `stencil_edit`: validate, draw an inline layout if given, spawn, and parse.
pub async fn run_edit(params: &EditParams) -> Result<EditResult, EditError> {
    // Clobber guard: refuse to replace an existing file the caller didn't opt into.
    if !params.overwrite && Path::new(&params.output).exists() {
        return Err(EditError::Runtime(format!(
            "output '{}' already exists; pass overwrite=true to replace it",
            params.output
        )));
    }

    // Materialize an inline layout to a temp file; keep the handle alive across the spawn.
    let mut layout_temp: Option<tempfile::NamedTempFile> = None;
    let layout_path: Option<String> = match &params.layout {
        None => None,
        Some(LayoutArg::Path(path)) => Some(path.clone()),
        Some(LayoutArg::Inline(layout)) => {
            let file = crate::layout::write_temp(layout)
                .map_err(|e| format!("could not write the inline layout to a temp file: {e}"))?;
            let path = file.path().to_string_lossy().into_owned();
            layout_temp = Some(file);
            Some(path)
        }
    };

    let argv = args::build_argv(params, layout_path.as_deref())?;
    let result = spawn(&argv).await;

    // Drop the temp file only after the CLI has run.
    drop(layout_temp);

    let output = result?;
    if !output.success {
        return Err(outcome::extract_errors(&output.stderr).into());
    }
    match outcome::parse_wrote(&output.stderr) {
        Some(w) => Ok(EditResult {
            path: w.path,
            width: w.width,
            height: w.height,
            remotes: outcome::parse_remotes(&output.stderr),
        }),
        None => Err(EditError::Runtime(format!(
            "the stencil CLI reported success but printed no 'wrote' line:\n{}",
            output.stderr.trim()
        ))),
    }
}

/// Run one `source_site` scrape: build the argv, spawn the CLI (which fetches the page,
/// parses its HTML, filters, and downloads the matches), and parse the multi-file result.
/// The CLI writes the files itself; this only maps its stderr into a structured result.
pub async fn run_scrape(params: &ScrapeParams) -> Result<ScrapeResult, EditError> {
    let argv = args::build_scrape_argv(params)?;
    let output = spawn(&argv).await?;
    if !output.success {
        return Err(outcome::extract_errors(&output.stderr).into());
    }
    let scraped = outcome::parse_scraped(&output.stderr);
    if scraped.files.is_empty() {
        // A zero-file success shouldn't happen (the CLI exits 1 with `no media matched`),
        // but guard it so the tool never reports an empty success.
        return Err(EditError::Runtime(format!(
            "the stencil CLI reported success but wrote no files:\n{}",
            output.stderr.trim()
        )));
    }
    Ok(ScrapeResult {
        dir: scraped.dir,
        host: scraped.host,
        files: scraped.files,
    })
}

/// Run one `stencil_probe`: render the source to a throwaway PNG and read its dimensions.
/// The CLI has no read-only metadata mode, so this decodes + re-encodes once.
pub async fn run_probe(input: &str) -> Result<(u32, u32), String> {
    let temp = tempfile::Builder::new()
        .prefix("stencil-probe-")
        .suffix(".png")
        .tempfile()
        .map_err(|e| format!("could not create a temp file for probing: {e}"))?;
    let out_path = temp.path().to_string_lossy().into_owned();

    let argv = vec!["-i".to_string(), input.to_string(), out_path];
    let output = spawn(&argv).await?;
    drop(temp);

    if !output.success {
        return Err(outcome::extract_errors(&output.stderr));
    }
    match outcome::parse_wrote(&output.stderr) {
        Some(w) => Ok((w.width, w.height)),
        None => Err("could not determine the image dimensions from the CLI output".into()),
    }
}

/// Locate the CLI and run it with the given argv, capturing stderr.
async fn spawn(argv: &[String]) -> Result<CliOutput, String> {
    let bin = locate::find_cli()?;
    let output = tokio::process::Command::new(&bin)
        .args(argv)
        .env("NO_COLOR", "1")
        .output()
        .await
        .map_err(|e| format!("failed to run the stencil CLI ({}): {e}", bin.display()))?;

    Ok(CliOutput {
        success: output.status.success(),
        stderr: String::from_utf8_lossy(&output.stderr).into_owned(),
    })
}
