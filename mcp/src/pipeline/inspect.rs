//! The CLI's report and project modes (`cli/CONTRACT.md` §6): each prints one JSON document on
//! stdout, so each runs capturing, over an argv its tool built — a failure is the CLI's own
//! `error:` lines. Only `--project-file` writes locally, so only it is spawned confined.

use std::borrow::Cow;

use serde_json::Value;

use super::runner::SERVER_TOKENS;
use crate::confine;
use crate::outcome;
use crate::pipeline::{CliOutput, CliRunner};

/// `--probe -i <input>`: format, size, alpha, bytes, and a video's duration and frames.
pub async fn probe<R: CliRunner>(runner: &R, argv: &[Cow<'static, str>]) -> Result<Value, String> {
    document(runner.run_capturing(argv, None).await?)
}

/// A `--server` mode (the project reads, `--project-update`, `--project-files`), the server's
/// token handed over in the child's environment rather than on its command line.
pub async fn projects<R: CliRunner>(
    runner: &R,
    argv: &[Cow<'static, str>],
    tokens: Option<&str>,
) -> Result<Value, String> {
    document(runner.run_with(argv, None, &token_env(tokens), true).await?)
}

/// `--project-file`, whose last token is its output: spawned inside `root` with that output
/// relative under `--confine-output`, as an edit is; the document's `path` is rejoined onto it.
pub async fn project_file<R: CliRunner>(
    runner: &R,
    argv: &[Cow<'static, str>],
    root: &str,
    tokens: Option<&str>,
) -> Result<Value, String> {
    let Some(run) = confine::confine(root, argv) else {
        return Err("error: refusing an unconfined run".to_string());
    };
    let env = token_env(tokens);
    let mut doc = document(runner.run_with(&run.argv, Some(&run.dir), &env, true).await?)?;
    if let Some(path) = doc["path"].as_str().map(str::to_string) {
        doc["path"] = Value::String(confine::rejoin(Some(root), path));
    }
    Ok(doc)
}

fn token_env(tokens: Option<&str>) -> Vec<(&'static str, String)> {
    tokens.map(|t| vec![(SERVER_TOKENS, t.to_string())]).unwrap_or_default()
}

fn document(output: CliOutput) -> Result<Value, String> {
    if !output.success {
        return Err(outcome::extract_errors(&output.stderr));
    }
    serde_json::from_str(output.stdout.trim())
        .map_err(|e| format!("the stencil CLI printed no JSON document: {e}"))
}
