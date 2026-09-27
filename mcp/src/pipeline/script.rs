//! The script modes that run nothing (`cli/CONTRACT.md` §4): check, plan and emit, each over
//! an argv its tool built; and the result a `--script` run (`run::script`) parses into.

use std::borrow::Cow;
use std::path::Path;

use crate::confine::{self, FLAG_CONFINE_OUTPUT};
use crate::outcome::{self, Diagnostic};
use crate::pipeline::CliRunner;

/// A successful script run: every image its `@save` ops wrote, every `.stencil` project,
/// and the `note:` lines the CLI printed along the way.
#[derive(Debug, Clone)]
pub struct ScriptResult {
    pub files: Vec<outcome::Wrote>,
    pub projects: Vec<String>,
    pub notes: Vec<String>,
}

/// What a check found; `valid` is false when any diagnostic is an error.
#[derive(Debug, Clone)]
pub struct ScriptCheck {
    pub valid: bool,
    pub diagnostics: Vec<Diagnostic>,
}

/// A script written out for another surface: the file and the target it was emitted for.
#[derive(Debug, Clone)]
pub struct Emitted {
    pub path: String,
    pub target: String,
}

/// `--script-check`: the diagnostics come back on stdout; a failure with no error among
/// them (an unreadable script) is the CLI's own `error:` lines.
pub async fn check<R: CliRunner>(
    runner: &R,
    argv: &[Cow<'static, str>],
    dir: &Path,
) -> Result<ScriptCheck, String> {
    let output = runner.run_capturing(argv, Some(dir)).await?;
    let diagnostics = outcome::parse_diagnostics(&output.stdout);
    if !output.success && !diagnostics.iter().any(|d| d.severity == "error") {
        return Err(outcome::extract_errors(&output.stderr));
    }
    Ok(ScriptCheck { valid: output.success, diagnostics })
}

/// `--script-plan`: the one JSON envelope on stdout, printed even when the script has errors.
pub async fn plan<R: CliRunner>(
    runner: &R,
    argv: &[Cow<'static, str>],
    dir: &Path,
) -> Result<serde_json::Value, String> {
    let output = runner.run_capturing(&confine::absolutize(argv), Some(dir)).await?;
    match serde_json::from_str::<serde_json::Value>(output.stdout.trim()) {
        Ok(envelope) if envelope.is_object() => Ok(envelope),
        _ if !output.success => Err(outcome::extract_errors(&output.stderr)),
        _ => Err("the stencil CLI printed no plan envelope".to_string()),
    }
}

/// `--script <file> --script-emit <out>` (`out` the argv's LAST token, absolute), spawned in
/// `root` with `out` relative to it under `--confine-output`.
pub async fn emit<R: CliRunner>(
    runner: &R,
    argv: &[Cow<'static, str>],
    root: &Path,
) -> Result<Emitted, String> {
    let (out, head) = argv.split_last().ok_or("an emit needs an output")?;
    let relative = confine::relative_to(root, Path::new(out.as_ref()))
        .filter(|rel| !rel.as_os_str().is_empty())
        .ok_or_else(|| format!("error: refusing to write outside '{}': '{out}'", root.display()))?;

    let mut confined = confine::absolutize(head);
    confined.insert(confined.len().saturating_sub(1), Cow::Borrowed(FLAG_CONFINE_OUTPUT));
    confined.push(Cow::Owned(relative.to_string_lossy().into_owned()));
    let output = runner.run(&confined, Some(root)).await?;
    if !output.success {
        let message = outcome::extract_errors(&output.stderr);
        return Err(match message.contains("--no-clobber:") {
            true => format!("{message}; pass overwrite=true to replace it"),
            false => message,
        });
    }
    let documents = outcome::parse_documents(&output.stderr).into_iter();
    let (path, target) = documents.into_iter().find(|(_, kind)| kind != "project").ok_or_else(|| {
        format!(
            "the stencil CLI reported success but printed no 'wrote' line:\n{}",
            output.stderr.trim()
        )
    })?;
    Ok(Emitted { path: confine::rejoin(root.to_str(), path), target })
}
