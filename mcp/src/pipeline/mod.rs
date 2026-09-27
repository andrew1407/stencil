//! Orchestration: turn typed parameters into a CLI run and a structured result.
//!
//! Mirrors `cli/src/pipeline.zig` on the wrapper side — locate the binary, materialize an
//! inline layout, spawn the CLI with `NO_COLOR=1`, map exit status + stderr to a result.
//! The spawn sits behind [`CliRunner`] in [`runner`], the orchestration in [`run`].

pub mod capture;
mod env;
pub mod inspect;
pub mod preview;
pub mod progress;
pub mod run;
mod runner;
pub mod script;

pub use env::{child_env, gui_env};
pub use runner::{CliOutput, CliRunner, ProcessRunner, SERVER_TOKENS};
pub use script::{Emitted, ScriptCheck, ScriptResult};

use std::borrow::Cow;

use crate::args::{EditError, EditParams, ScrapeParams, ScriptParams};
use crate::outcome::{self, ScrapedFile};

/// A successful edit: the resolved output path, the final image dimensions (none for a
/// `.stencil` project), and any collaboration-server deliveries the CLI performed.
#[derive(Debug, Clone)]
pub struct EditResult {
    pub path: String,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub remotes: Vec<outcome::Remote>,
}

impl EditResult {
    /// The human-readable head of the tool summary: the CLI's own `wrote` line followed by
    /// one line per collaboration-server delivery. The handler appends the surface notes.
    pub fn summary(&self) -> String {
        let mut summary = match (self.width, self.height) {
            (Some(w), Some(h)) => format!("wrote {} ({w}x{h})", self.path),
            _ => format!("wrote {} (project)", self.path),
        };
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

/// Run one `stencil_edit`: validate, draw an inline layout if given, spawn, and parse.
pub async fn run_edit(params: &EditParams) -> Result<EditResult, EditError> {
    run::edit(&ProcessRunner, params).await
}

/// Run one contract §2.1 `save`: the same pipeline with a `.stencil` output, which the CLI
/// bundles as a project and reports without dimensions. Returns the written path.
pub async fn run_project(params: &EditParams) -> Result<String, EditError> {
    run::project(&ProcessRunner, params).await
}

/// Run one `source_site` scrape inside `root`: the CLI fetches the page, filters, and
/// downloads the matches; its stderr becomes a structured result.
pub async fn run_scrape(params: &ScrapeParams, root: &str) -> Result<ScrapeResult, EditError> {
    run::scrape(&ProcessRunner, params, root).await
}

/// Run one `stencil_script`: `stencil --script <file>`, always inside the sandbox root, and
/// parse each `@save`'s `wrote` line back out of stderr.
pub async fn run_script(
    params: &ScriptParams,
    script_file: &str,
) -> Result<ScriptResult, EditError> {
    run::script(&ProcessRunner, params, script_file).await
}

/// Run one `stencil_probe` argv (`--probe -i <input>`) and hand back the CLI's JSON document.
pub async fn run_probe(argv: &[Cow<'static, str>]) -> Result<serde_json::Value, String> {
    inspect::probe(&ProcessRunner, argv).await
}

/// Run one `--server` argv (`--list-projects`, `--project-info`, `--project-update`,
/// `--project-files`) with the operator's `origin=token` pair for that server, and hand back
/// the CLI's JSON document.
pub async fn run_projects(
    argv: &[Cow<'static, str>],
    tokens: Option<&str>,
) -> Result<serde_json::Value, String> {
    inspect::projects(&ProcessRunner, argv, tokens).await
}

/// Run one `stencil_project_file` argv (`--project-file <id> <kind> <output>`) confined to
/// `root`, with the operator's `origin=token` pair, and hand back the CLI's document.
pub async fn run_project_file(
    argv: &[Cow<'static, str>],
    root: &str,
    tokens: Option<&str>,
) -> Result<serde_json::Value, String> {
    inspect::project_file(&ProcessRunner, argv, root, tokens).await
}

/// Render a written image (inside `root`) as a PNG no longer than 512 px a side — the
/// opt-in `preview` of `stencil_edit` and `stencil_script`.
pub async fn render_preview(image: &str, root: &str) -> Result<preview::Preview, String> {
    preview::render(&ProcessRunner, image, root).await
}

/// Render `input` through the CLI's contour filter and return the PNG bytes — the §7 edge
/// map. Best-effort: any failure yields `None` and the prompt still runs.
pub async fn render_edge_map(input: &str) -> Option<Vec<u8>> {
    run::edge_map(&ProcessRunner, input).await
}
