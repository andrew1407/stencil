//! The `.stc` tools: `stencil_script` runs one script confined to one directory; `check`,
//! `plan` and `emit` read a script without running it.

pub mod check;
pub mod emit;
pub mod plan;

use std::io::Write;

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use schemars::JsonSchema;
use serde::Serialize;

use super::preview::{PreviewNote, Previews, MAX_SCRIPT_PREVIEWS};
use super::{err_result, ok_result};
use crate::args::{ScriptParams, ScriptSource};
use crate::confine::Roots;
use crate::outcome::Wrote;
use crate::pipeline;

/// The `stencil_script` structured payload: every image the script's `@save` ops wrote (as
/// `Wrote`'s own `Serialize` shapes it), every `.stencil` project, and the `note:` lines;
/// `previews` is there only when the call asked for them.
#[derive(Serialize, JsonSchema)]
pub struct ScriptPayload<'a> {
    files: &'a [Wrote],
    projects: &'a [String],
    notes: &'a [String],
    #[serde(skip_serializing_if = "Option::is_none")]
    previews: Option<&'a [PreviewNote]>,
}

/// The tool's whole body: guard the parameters, place the run inside the roots, materialize
/// inline text, run, then report.
pub async fn run(roots: &Roots, mut params: ScriptParams) -> Result<CallToolResult, McpError> {
    // Refuse before a temp file exists; `build_script_argv` guards the spawn again.
    if let Err(error) = params.validate() {
        return Ok(err_result(error.to_string()));
    }
    let dir = params.root().to_string();
    match roots.place("output_dir", &dir) {
        Ok((_, dir)) => params.output_dir = Some(dir.to_string_lossy().into_owned()),
        Err(message) => return Ok(err_result(message)),
    }
    params.input = params.input.as_deref().map(|input| roots.resolve(input));
    let file = match ScriptFile::new(&params.script_text, &params.script_path, roots) {
        Ok(file) => file,
        Err(message) => return Ok(err_result(message)),
    };

    let result = match pipeline::run_script(&params, &file.path).await {
        Ok(result) => result,
        Err(error) => return Ok(err_result(error.to_string())),
    };

    // A summary in the CLI's own vocabulary: one `wrote` line per save, then its notes.
    use std::fmt::Write as _;
    let mut summary = String::new();
    for file in &result.files {
        let _ = writeln!(summary, "wrote {} ({}x{})", file.path, file.width, file.height);
    }
    for project in &result.projects {
        let _ = writeln!(summary, "wrote {project} (project)");
    }
    for note in &result.notes {
        let _ = writeln!(summary, "note: {note}");
    }
    let written = result.files.len() + result.projects.len();
    let _ = write!(summary, "the script wrote {written} file(s)");

    let previews = match params.preview {
        true => Some(preview(&result.files, params.root()).await),
        false => None,
    };
    for line in previews.iter().flat_map(|p| &p.lines) {
        let _ = write!(summary, "\n{line}");
    }
    let payload = ScriptPayload {
        files: &result.files,
        projects: &result.projects,
        notes: &result.notes,
        previews: previews.as_ref().map(|p| p.notes.as_slice()),
    };
    let tool_result = ok_result(summary, payload)?;
    Ok(match previews {
        Some(previews) => previews.attach(tool_result),
        None => tool_result,
    })
}

/// The first images the script wrote, previewed; past the cap the summary says how many.
async fn preview(files: &[Wrote], root: &str) -> Previews {
    let first = files.iter().take(MAX_SCRIPT_PREVIEWS);
    let paths: Vec<&str> = first.map(|f| f.path.as_str()).collect();
    let mut previews = Previews::render(&paths, root).await;
    if files.len() > paths.len() {
        let shown = paths.len();
        previews.lines.push(format!("preview: the first {shown} of {} images", files.len()));
    }
    previews
}

/// The script file a CLI run names: the caller's own `.stc` anchored on the roots, or inline
/// text as a temp `.stc` that lives as long as this value.
pub(super) struct ScriptFile {
    pub path: String,
    pub inline: bool,
    _temp: Option<tempfile::NamedTempFile>,
}

impl ScriptFile {
    pub fn from_source(source: &ScriptSource, roots: &Roots) -> Result<Self, String> {
        Self::new(&source.script_text, &source.script_path, roots)
    }

    pub fn new(
        text: &Option<String>,
        path: &Option<String>,
        roots: &Roots,
    ) -> Result<Self, String> {
        if let Some(path) = path {
            return Ok(ScriptFile { path: roots.resolve(path), inline: false, _temp: None });
        }
        let temp = write_temp(text.as_deref().unwrap_or_default())?;
        let path = temp.path().to_string_lossy().into_owned();
        Ok(ScriptFile { path, inline: true, _temp: Some(temp) })
    }
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
        let projects = vec!["/tmp/run/a.stencil".to_string()];
        let notes = vec!["the script saved nothing — add a @save".to_string()];
        let payload =
            ScriptPayload { files: &files, projects: &projects, notes: &notes, previews: None };
        let value = serde_json::to_value(payload).unwrap();

        assert_eq!(
            value["files"][0],
            json!({"path":"/tmp/run/a-stencil.png","width":4,"height":8})
        );
        assert_eq!(value["projects"][0], "/tmp/run/a.stencil");
        assert_eq!(value["notes"][0], "the script saved nothing — add a @save");
    }

    #[test]
    fn a_script_that_wrote_nothing_still_sends_empty_arrays() {
        let payload = ScriptPayload { files: &[], projects: &[], notes: &[], previews: None };
        let value = serde_json::to_value(payload).unwrap();
        assert_eq!(value, json!({"files":[],"projects":[],"notes":[]}));
    }
}
