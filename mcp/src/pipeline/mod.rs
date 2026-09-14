//! Orchestration: turn typed parameters into a CLI run and a structured result.
//!
//! Mirrors the role of `cli/src/pipeline.zig` on the wrapper side — it locates the binary,
//! materializes an inline layout, spawns the CLI (with `NO_COLOR=1`), and maps the exit
//! status + stderr into a result or an error. All pixel work happens in the CLI/core.
//!
//! The spawn itself sits behind [`CliRunner`] in [`runner`], and the orchestration around
//! it in [`run`] — so a test can drive the whole flow without a Zig toolchain.

pub mod run;
mod runner;

pub use runner::{CliOutput, CliRunner, ProcessRunner};

use crate::args::{EditError, EditParams, ScrapeParams, ScriptParams};
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

/// A successful script run: every file its `@save` ops wrote, plus the `note:` lines the CLI
/// printed along the way (an unmatched `@source`, a script that saved nothing).
#[derive(Debug, Clone)]
pub struct ScriptResult {
    pub files: Vec<outcome::Wrote>,
    pub notes: Vec<String>,
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

/// Run one `source_site` scrape: build the argv, spawn the CLI (which fetches the page,
/// filters, and downloads the matches), and map its stderr into a structured result.
pub async fn run_scrape(params: &ScrapeParams) -> Result<ScrapeResult, EditError> {
    run::scrape(&ProcessRunner, params).await
}

/// Run one `stencil_script`: `stencil --script <file>`, always inside the sandbox root, and
/// parse each `@save`'s `wrote` line back out of stderr.
pub async fn run_script(
    params: &ScriptParams,
    script_file: &str,
) -> Result<ScriptResult, EditError> {
    run::script(&ProcessRunner, params, script_file).await
}

/// Run one `stencil_probe`. A local PNG/GIF/BMP/JPEG/WebP answers out of its own header;
/// anything else (a URL, a video, an unreadable header) is rendered to a throwaway PNG,
/// since the CLI has no read-only metadata mode.
pub async fn run_probe(input: &str) -> Result<(u32, u32), String> {
    run::probe(&ProcessRunner, input).await
}

/// Render `input` through the CLI's contour filter and return the PNG bytes — the §7 edge
/// map. Best-effort: any failure yields `None` and the prompt still runs.
pub async fn render_edge_map(input: &str) -> Option<Vec<u8>> {
    run::edge_map(&ProcessRunner, input).await
}
