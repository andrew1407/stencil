//! What every run does around the spawn: the guards, the temp files, and the parsing of
//! the CLI's own output — generic over [`CliRunner`], so a test drives the whole flow with
//! its own runner and no Zig toolchain. [`super`]'s entry points bind the real one.

use std::borrow::Cow;

use super::progress::{self, Sink};
use super::runner::SERVER_TOKENS;
use super::{EditResult, ScrapeResult, ScriptResult};
use crate::args::{self, EditError, EditParams, LayoutArg, ScrapeParams, ScriptParams};
use crate::confine;
use crate::layout::Layout;
use crate::outcome;
use crate::pipeline::CliRunner;

pub async fn edit<R: CliRunner>(
    runner: &R,
    params: &EditParams,
) -> Result<EditResult, EditError> {
    let stderr = run_cli(runner, params).await?;
    let (path, dims) = match outcome::parse_wrote(&stderr) {
        Some(w) => (w.path, Some((w.width, w.height))),
        // A `.stencil` output is a project: the CLI's `wrote … (project)` line, no size.
        None => match outcome::parse_wrote_project(&stderr) {
            Some(path) => (path, None),
            None => {
                return Err(EditError::Runtime(format!(
                    "the stencil CLI reported success but printed no 'wrote' line:\n{}",
                    stderr.trim()
                )))
            }
        },
    };
    Ok(EditResult {
        path: confine::rejoin(params.confine_root.as_deref(), path),
        width: dims.map(|d| d.0),
        height: dims.map(|d| d.1),
        remotes: outcome::parse_remotes(&stderr),
    })
}

/// Run one contract §2.1 `save`: the same pipeline with a `.stencil` output, which the CLI
/// bundles as a project and reports without dimensions. Returns the written path.
pub async fn project<R: CliRunner>(
    runner: &R,
    params: &EditParams,
) -> Result<String, EditError> {
    let stderr = run_cli(runner, params).await?;
    let path = outcome::parse_wrote_project(&stderr).ok_or_else(|| {
        EditError::Runtime(format!(
            "the stencil CLI reported success but wrote no project:\n{}",
            stderr.trim()
        ))
    })?;
    Ok(confine::rejoin(params.confine_root.as_deref(), path))
}

/// One CLI invocation of the edit pipeline: inline-layout temp file, argv, spawn — returning
/// the successful run's stderr for the caller to parse.
async fn run_cli<R: CliRunner>(runner: &R, params: &EditParams) -> Result<String, EditError> {
    // Every run is confined: a caller that named no root is refused, never run open.
    let Some(root) = params.confine_root.as_deref() else {
        return Err(EditError::Runtime("error: refusing an unconfined run".to_string()));
    };

    // Materialize an inline layout to a temp file; keep the handle alive across the spawn.
    let mut layout_temp: Option<tempfile::NamedTempFile> = None;
    let layout_path: Option<String> = match &params.layout {
        None => None,
        Some(LayoutArg::Path(text)) if !text.trim_start().starts_with('{') => Some(text.clone()),
        Some(arg) => {
            let file = crate::layout::write_temp(&*inline_layout(arg)?)
                .map_err(|e| format!("could not write the inline layout to a temp file: {e}"))?;
            let path = file.path().to_string_lossy().into_owned();
            layout_temp = Some(file);
            Some(path)
        }
    };

    let mut argv = args::build_argv(params, layout_path.as_deref())?;
    // The CLI owns the clobber check: it knows the name it will write, extension filled in.
    if !params.overwrite {
        argv = args::with_no_clobber(argv);
    }
    let env: Vec<(&'static str, String)> =
        params.server_tokens.iter().map(|t| (SERVER_TOKENS, t.clone())).collect();
    // Spawned INSIDE its sandbox root with `--confine-output`, so the CLI refuses an
    // escaping destination itself.
    let result = match confine::confine(root, &argv) {
        Some(run) => runner.run_with(&run.argv, Some(&run.dir), &env, false).await,
        None => Err(format!("error: refusing to write outside '{root}': '{}'", params.output)),
    };

    drop(layout_temp);

    let output = result?;
    if !output.success {
        let message = outcome::extract_errors(&output.stderr);
        return Err(match message.contains("--no-clobber:") {
            true => format!("{message}; pass overwrite=true to replace it"),
            false => message,
        }
        .into());
    }
    Ok(output.stderr)
}

/// The layout object itself — inline, or a JSON string a caller serialized.
fn inline_layout(arg: &LayoutArg) -> Result<Cow<'_, Layout>, EditError> {
    match arg {
        LayoutArg::Inline(layout) => Ok(Cow::Borrowed(layout)),
        LayoutArg::Path(text) => serde_json::from_str(text).map(Cow::Owned).map_err(|e| {
            EditError::Runtime(format!("`layout` is not a layout JSON object: {e}"))
        }),
    }
}

/// Run one `source_site` scrape, spawned inside `root` with `--confine-output`: the CLI
/// fetches the page, filters, and downloads the matches into the output directory.
pub async fn scrape<R: CliRunner>(
    runner: &R,
    params: &ScrapeParams,
    root: &str,
) -> Result<ScrapeResult, EditError> {
    let argv = args::build_scrape_argv(params)?;
    let Some(run) = confine::confine_output_dir(root, &argv) else {
        let dir = params.output.as_deref().unwrap_or(".");
        return Err(format!("error: refusing to write outside '{root}': '{dir}'").into());
    };
    let output = runner.run(&run.argv, Some(&run.dir)).await?;
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
    // The CLI reports paths relative to the root it ran in; hand back absolutes.
    let at_root = |path: String| confine::rejoin(Some(root), path);
    Ok(ScrapeResult {
        dir: scraped.dir.map(at_root),
        host: scraped.host,
        files: scraped
            .files
            .into_iter()
            .map(|f| outcome::ScrapedFile { path: at_root(f.path), ..f })
            .collect(),
    })
}

/// Run one `.stc` through the CLI's `--script` mode. The script names its own outputs, so
/// the run is ALWAYS confined: it spawns inside the sandbox root with `--confine-output`.
pub async fn script<R: CliRunner>(
    runner: &R,
    params: &ScriptParams,
    script_file: &str,
) -> Result<ScriptResult, EditError> {
    let argv = args::build_script_argv(params, script_file)?;
    let root = params.root();
    std::fs::create_dir_all(root)
        .map_err(|e| format!("could not create the output directory '{root}': {e}"))?;

    let run = confine::confine_dir(root, &argv);
    let output = runner.run(&run.argv, Some(&run.dir)).await?;
    if !output.success {
        return Err(outcome::extract_errors(&output.stderr).into());
    }
    // Every path the CLI reported is relative to the root it ran in; hand back absolutes.
    let at_root = |path: String| confine::rejoin(Some(root), path);
    let files = outcome::parse_all_wrote(&output.stderr);
    let documents = outcome::parse_documents(&output.stderr);
    let projects = documents.into_iter().filter(|(_, kind)| kind == "project");
    Ok(ScriptResult {
        files: files.into_iter().map(|w| outcome::Wrote { path: at_root(w.path), ..w }).collect(),
        projects: projects.map(|(path, _)| at_root(path)).collect(),
        notes: outcome::parse_notes(&output.stderr),
    })
}

/// Render `input` through the contour filter and return the PNG bytes — the §7 edge map,
/// written confined inside a private temp directory that is gone once they are read.
pub async fn edge_map<R: CliRunner>(runner: &R, input: &str) -> Option<Vec<u8>> {
    let dir = tempfile::Builder::new().prefix("stencil-edgemap-").tempdir().ok()?;
    let out = dir.path().join("edge-map.png");
    let argv = [
        Cow::Borrowed("-i"),
        Cow::Owned(input.to_string()),
        Cow::Borrowed("--filter"),
        Cow::Borrowed("contour"),
        Cow::Owned(out.to_string_lossy().into_owned()),
    ];
    let run = confine::confine(&dir.path().to_string_lossy(), &argv)?;
    // Its `wrote` line names the scratch file, which is no step of the call.
    let quiet = Some(Sink::new(|_| {}));
    let output = progress::scope(quiet, runner.run(&run.argv, Some(&run.dir))).await.ok()?;
    if !output.success {
        return None;
    }
    // Off the async runtime: a render is megabytes.
    tokio::task::spawn_blocking(move || std::fs::read(out).ok()).await.ok().flatten()
}
