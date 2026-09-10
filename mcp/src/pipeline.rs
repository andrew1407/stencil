//! Orchestration: turn typed parameters into a CLI run and a structured result.
//!
//! Mirrors the role of `cli/src/pipeline.zig` on the wrapper side — it locates the binary,
//! materializes an inline layout, spawns the CLI (with `NO_COLOR=1`), and maps the exit
//! status + stderr into a result or an error. All pixel work happens in the CLI/core.

use std::path::Path;

use crate::args::{self, EditError, EditParams, LayoutArg, ScrapeParams};
use crate::confine;
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
    /// line per collaboration-server delivery. The handler appends the surface notes.
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
/// line, when present) plus every downloaded file, ready to serialize.
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
    let stderr = run_cli(params).await?;
    match outcome::parse_wrote(&stderr) {
        Some(w) => Ok(EditResult {
            path: confine::rejoin(params.confine_root.as_deref(), w.path),
            width: w.width,
            height: w.height,
            remotes: outcome::parse_remotes(&stderr),
        }),
        None => Err(EditError::Runtime(format!(
            "the stencil CLI reported success but printed no 'wrote' line:\n{}",
            stderr.trim()
        ))),
    }
}

/// Run one contract §2.1 `save`: the same pipeline with a `.stencil` output, which the CLI
/// bundles as a project and reports without dimensions. Returns the written path.
pub async fn run_project(params: &EditParams) -> Result<String, EditError> {
    let stderr = run_cli(params).await?;
    let path = outcome::parse_wrote_project(&stderr).ok_or_else(|| {
        EditError::Runtime(format!(
            "the stencil CLI reported success but wrote no project:\n{}",
            stderr.trim()
        ))
    })?;
    Ok(confine::rejoin(params.confine_root.as_deref(), path))
}

/// One CLI invocation of the edit pipeline: clobber guard, inline-layout temp file, argv,
/// spawn — returning the successful run's stderr for the caller to parse.
async fn run_cli(params: &EditParams) -> Result<String, EditError> {
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
    // A confined run is spawned INSIDE its sandbox root with `--confine-output` — defence in
    // depth behind the executor's own path sandbox. No root means an unconfined run.
    let result = match params.confine_root.as_deref().map(|r| confine::confine(r, &argv)) {
        None => spawn(&argv, None).await,
        Some(Some(run)) => spawn(&run.argv, Some(&run.dir)).await,
        Some(None) => Err(format!("error: refusing to write outside '{}'", params.output)),
    };

    // Drop the temp file only after the CLI has run.
    drop(layout_temp);

    let output = result?;
    if !output.success {
        return Err(outcome::extract_errors(&output.stderr).into());
    }
    Ok(output.stderr)
}

/// Run one `source_site` scrape: build the argv, spawn the CLI (which fetches the page,
/// filters, and downloads the matches), and map its stderr into a structured result.
pub async fn run_scrape(params: &ScrapeParams) -> Result<ScrapeResult, EditError> {
    let argv = args::build_scrape_argv(params)?;
    let output = spawn(&argv, None).await?;
    if !output.success {
        return Err(outcome::extract_errors(&output.stderr).into());
    }
    let scraped = outcome::parse_scraped(&output.stderr);
    if scraped.files.is_empty() {
        // The CLI exits 1 on `no media matched`; guard so success is never empty.
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
    let output = spawn(&argv, None).await?;
    drop(temp);

    if !output.success {
        return Err(outcome::extract_errors(&output.stderr));
    }
    match outcome::parse_wrote(&output.stderr) {
        Some(w) => Ok((w.width, w.height)),
        None => Err("could not determine the image dimensions from the CLI output".into()),
    }
}

/// Render `input` through the CLI's contour filter into a temp PNG and return its bytes —
/// the §7 edge map. Best-effort: any failure yields `None` and the prompt still runs.
pub async fn render_edge_map(input: &str) -> Option<Vec<u8>> {
    let temp = tempfile::Builder::new()
        .prefix("stencil-edgemap-")
        .suffix(".png")
        .tempfile()
        .ok()?;
    let out_path = temp.path().to_string_lossy().into_owned();

    let argv = vec![
        "-i".to_string(),
        input.to_string(),
        "--filter".to_string(),
        "contour".to_string(),
        out_path,
    ];
    let output = spawn(&argv, None).await.ok()?;
    if !output.success {
        return None;
    }
    std::fs::read(temp.path()).ok()
}

/// Locate the CLI and run it with the given argv, capturing stderr — under the
/// `config::cli_timeout()` deadline (the bot's `ProcessRunner` rule), so a hung CLI can
/// never pin an MCP tool call forever. Its stdin is `/dev/null`: our own stdin is the
/// JSON-RPC channel and the CLI must never read from it.
async fn spawn(argv: &[String], dir: Option<&Path>) -> Result<CliOutput, String> {
    let bin = locate::find_cli()?;
    let deadline = crate::config::cli_timeout();
    let failed = |e| format!("failed to run the stencil CLI ({}): {e}", bin.display());
    let child = tokio::process::Command::new(&bin)
        // `.` is the inherited working directory; a confined run names its sandbox root.
        .current_dir(dir.unwrap_or(Path::new(".")))
        .args(argv)
        .env("NO_COLOR", "1")
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        // On expiry the timeout drops the wait future, which drops the child; this kills it.
        .kill_on_drop(true)
        .spawn()
        .map_err(failed)?;

    let output = match tokio::time::timeout(deadline, child.wait_with_output()).await {
        Ok(result) => result.map_err(failed)?,
        Err(_) => {
            return Err(format!(
                "error: the stencil CLI timed out after {}s and was terminated",
                deadline.as_secs()
            ))
        }
    };
    Ok(CliOutput {
        success: output.status.success(),
        stderr: String::from_utf8_lossy(&output.stderr).into_owned(),
    })
}
