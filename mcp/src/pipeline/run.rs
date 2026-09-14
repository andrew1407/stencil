//! What every run does around the spawn: the guards, the temp files, and the parsing of
//! the CLI's own output — generic over [`CliRunner`], so a test drives the whole flow with
//! its own runner and no Zig toolchain. [`super`]'s entry points bind the real one.

use std::borrow::Cow;
use std::path::Path;

use super::{EditResult, ScrapeResult, ScriptResult};
use crate::args::{self, EditError, EditParams, LayoutArg, ScrapeParams, ScriptParams};
use crate::confine;
use crate::outcome;
use crate::pipeline::CliRunner;

pub async fn edit<R: CliRunner>(
    runner: &R,
    params: &EditParams,
) -> Result<EditResult, EditError> {
    let stderr = run_cli(runner, params).await?;
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

/// One CLI invocation of the edit pipeline: clobber guard, inline-layout temp file, argv,
/// spawn — returning the successful run's stderr for the caller to parse.
async fn run_cli<R: CliRunner>(runner: &R, params: &EditParams) -> Result<String, EditError> {
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
        None => runner.run(&argv, None).await,
        Some(Some(run)) => runner.run(&run.argv, Some(&run.dir)).await,
        Some(None) => Err(format!("error: refusing to write outside '{}'", params.output)),
    };

    drop(layout_temp);

    let output = result?;
    if !output.success {
        return Err(outcome::extract_errors(&output.stderr).into());
    }
    Ok(output.stderr)
}

/// Run one `source_site` scrape: build the argv, spawn the CLI (which fetches the page,

pub async fn scrape<R: CliRunner>(
    runner: &R,
    params: &ScrapeParams,
) -> Result<ScrapeResult, EditError> {
    let argv = args::build_scrape_argv(params)?;
    let output = runner.run(&argv, None).await?;
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

/// Run one `.stc` through the CLI's `--script` mode. The script names its own outputs, so
/// the run is ALWAYS confined: it spawns inside the sandbox root with `--confine-output`,
/// which refuses every `@save` that would climb out of it.
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
    let files = outcome::parse_all_wrote(&output.stderr)
        .into_iter()
        .map(|w| outcome::Wrote { path: confine::rejoin(Some(root), w.path), ..w })
        .collect();
    Ok(ScriptResult { files, notes: outcome::parse_notes(&output.stderr) })
}

/// Run one `stencil_probe`. A local PNG/GIF/BMP/JPEG/WebP answers out of its own header;
/// anything else (a URL, a video, an unreadable header) is rendered to a throwaway PNG,

pub async fn probe<R: CliRunner>(runner: &R, input: &str) -> Result<(u32, u32), String> {
    if let Some(dims) = crate::imagesize::read_dimensions(input).await {
        return Ok(dims);
    }
    let (_temp, stderr) = render_temp(runner, input, "stencil-probe-", &[]).await?;
    match outcome::parse_wrote(&stderr) {
        Some(w) => Ok((w.width, w.height)),
        None => Err("could not determine the image dimensions from the CLI output".into()),
    }
}

pub async fn edge_map<R: CliRunner>(runner: &R, input: &str) -> Option<Vec<u8>> {
    let filter = [Cow::Borrowed("--filter"), Cow::Borrowed("contour")];
    let (temp, _) = render_temp(runner, input, "stencil-edgemap-", &filter).await.ok()?;
    // Off the async runtime: a render is megabytes.
    let path = temp.path().to_path_buf();
    tokio::task::spawn_blocking(move || std::fs::read(path).ok()).await.ok().flatten()
}

/// Render `input` to a fresh temp PNG (`extra` rides between the input and the output) and
/// hand back the live temp file plus the run's stderr.
async fn render_temp<R: CliRunner>(
    runner: &R,
    input: &str,
    prefix: &str,
    extra: &[Cow<'static, str>],
) -> Result<(tempfile::NamedTempFile, String), String> {
    let temp = tempfile::Builder::new()
        .prefix(prefix)
        .suffix(".png")
        .tempfile()
        .map_err(|e| format!("could not create a temp file for probing: {e}"))?;
    let mut argv = vec![Cow::Borrowed("-i"), Cow::Owned(input.to_string())];
    argv.extend_from_slice(extra);
    argv.push(temp.path().to_string_lossy().into_owned().into());
    let output = runner.run(&argv, None).await?;
    if !output.success {
        return Err(outcome::extract_errors(&output.stderr));
    }
    Ok((temp, output.stderr))
}
