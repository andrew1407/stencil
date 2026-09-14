//! `stencil_script`: run one `.stc` script through the CLI, confined to one directory.

use std::io::Write;

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use serde::Serialize;

use super::{err_result, ok_result};
use crate::args::ScriptParams;
use crate::outcome::Wrote;
use crate::pipeline;

/// The `stencil_script` structured payload: every file the script's `@save` ops wrote (as
/// `Wrote`'s own `Serialize` shapes it) plus the CLI's `note:` lines.
#[derive(Serialize)]
struct ScriptPayload<'a> {
    files: &'a [Wrote],
    notes: &'a [String],
}

/// The tool's whole body: guard the parameters, materialize inline text, run, then report.
pub async fn run(params: ScriptParams) -> Result<CallToolResult, McpError> {
    // Refuse before a temp file exists; `build_script_argv` guards the spawn again.
    if let Err(error) = params.validate() {
        return Ok(err_result(error.to_string()));
    }

    // Inline text becomes a temp `.stc`; the handle stays alive across the spawn.
    let mut temp = None;
    let file = match &params.script_path {
        Some(path) => path.clone(),
        None => match write_temp(params.script_text.as_deref().unwrap_or_default()) {
            Ok(handle) => {
                let path = handle.path().to_string_lossy().into_owned();
                temp = Some(handle);
                path
            }
            Err(message) => return Ok(err_result(message)),
        },
    };

    let outcome = pipeline::run_script(&params, &file).await;
    drop(temp);
    let result = match outcome {
        Ok(result) => result,
        Err(error) => return Ok(err_result(error.to_string())),
    };

    // A summary in the CLI's own vocabulary: one `wrote` line per save, then its notes.
    use std::fmt::Write as _;
    let mut summary = String::new();
    for file in &result.files {
        let _ = writeln!(summary, "wrote {} ({}x{})", file.path, file.width, file.height);
    }
    for note in &result.notes {
        let _ = writeln!(summary, "note: {note}");
    }
    let _ = write!(summary, "the script wrote {} file(s)", result.files.len());

    let payload = ScriptPayload {
        files: &result.files,
        notes: &result.notes,
    };
    ok_result(summary, payload)
}

/// Write inline script text to a temp `.stc` the CLI can open by path.
fn write_temp(text: &str) -> Result<tempfile::NamedTempFile, String> {
    let mut handle = tempfile::Builder::new()
        .prefix("stencil-script-")
        .suffix(".stc")
        .tempfile()
        .map_err(|e| format!("could not create a temp file for the script: {e}"))?;
    handle
        .write_all(text.as_bytes())
        .and_then(|()| handle.flush())
        .map_err(|e| format!("could not write the script to a temp file: {e}"))?;
    Ok(handle)
}

#[cfg(test)]
mod tests {
    //! The script payload shape (pure) — the contract with a calling agent.

    use super::*;
    use serde_json::json;

    #[test]
    fn script_payload_shape() {
        let files = vec![Wrote { path: "/tmp/run/a-stencil.png".into(), width: 4, height: 8 }];
        let notes = vec!["the script saved nothing — add a @save".to_string()];
        let value = serde_json::to_value(ScriptPayload { files: &files, notes: &notes }).unwrap();

        assert_eq!(
            value["files"][0],
            json!({"path":"/tmp/run/a-stencil.png","width":4,"height":8})
        );
        assert_eq!(value["notes"][0], "the script saved nothing — add a @save");
    }

    #[test]
    fn a_script_that_wrote_nothing_still_sends_empty_arrays() {
        let value = serde_json::to_value(ScriptPayload { files: &[], notes: &[] }).unwrap();
        assert_eq!(value, json!({"files":[],"notes":[]}));
    }
}
