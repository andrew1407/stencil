//! The `stencil_script` tool: its parameters, their guards, and its argv.

use schemars::JsonSchema;
use serde::Deserialize;

use super::argv::{Argv, ArgvBuilder};
use super::errors::EditError;
use super::flags::{FLAG_INPUT, FLAG_SCRIPT};

/// The largest inline script accepted. The language's own caps (`MAX_LINES`, `MAX_TOKENS`)
/// sit far above it; this bounds what crosses the JSON-RPC channel before a file is written.
pub const MAX_SCRIPT_BYTES: usize = 256 * 1024;

/// Parameters for the `stencil_script` tool — run one `.stc` through the CLI's `--script`
/// mode (`cli/CONTRACT.md` §4). This adapter only names the file and the sandbox.
#[derive(Debug, Default, Deserialize, JsonSchema)]
pub struct ScriptParams {
    /// The script itself, as `.stc` source text. Written to a temporary `.stc` file for the
    /// run. Mutually exclusive with `script_path`; at most 256 KiB.
    #[serde(default)]
    pub script_text: Option<String>,

    /// Path to an existing `.stc` file to run instead of inline text. Mutually exclusive
    /// with `script_text`.
    #[serde(default)]
    pub script_path: Option<String>,

    /// Working image for a script with no `@source` block: a local path or an `http(s)://`
    /// URL, passed to the CLI as `-i`. Ignored by a script that names its own sources.
    #[serde(default)]
    pub input: Option<String>,

    /// Directory every `@save` must write inside, itself inside the server's roots (created
    /// if missing; defaults to the first root). Use relative `@save` targets.
    #[serde(default)]
    pub output_dir: Option<String>,

    /// Also return the first images the script wrote (up to 4) as PNG thumbnails (longer side
    /// at most 512 px) in image content blocks. Defaults to false.
    #[serde(default)]
    pub preview: bool,
}

impl ScriptParams {
    /// The sandbox root this run is confined to.
    pub fn root(&self) -> &str {
        match self.output_dir.as_deref().map(str::trim) {
            Some(dir) if !dir.is_empty() => dir,
            _ => ".",
        }
    }

    /// Every guard that can fail before a file is written or a process spawned: exactly one
    /// script source, a bounded inline script, a `.stc` path, and no dash-leading value.
    pub fn validate(&self) -> Result<(), EditError> {
        validate_source(&self.script_text, &self.script_path)?;
        if let Some(input) = &self.input {
            dash_free("input", input)?;
        }
        if let Some(dir) = &self.output_dir {
            dash_free("output_dir", dir)?;
        }
        Ok(())
    }
}

/// Exactly one of inline text (bounded, non-blank) or a `.stc` path that is not a flag.
pub(super) fn validate_source(
    text: &Option<String>,
    path: &Option<String>,
) -> Result<(), EditError> {
    match (text, path) {
        (Some(_), Some(_)) => return Err(EditError::ScriptSourceConflict),
        (None, None) => return Err(EditError::NoScript),
        _ => {}
    }
    if let Some(text) = text {
        if text.trim().is_empty() {
            return Err(EditError::NoScript);
        }
        if text.len() > MAX_SCRIPT_BYTES {
            return Err(EditError::ScriptTooLarge(text.len()));
        }
    }
    if let Some(path) = path {
        dash_free("script_path", path)?;
        if !has_stc_suffix(path) {
            return Err(EditError::ScriptNotStc(path.clone()));
        }
    }
    Ok(())
}

/// A `.stc` extension, matched case-insensitively — only the extension: the language's own
/// paths are case-sensitive, but a file name is the operating system's business.
fn has_stc_suffix(path: &str) -> bool {
    std::path::Path::new(path)
        .extension()
        .is_some_and(|ext| ext.eq_ignore_ascii_case("stc"))
}

pub(super) fn dash_free(field: &'static str, value: &str) -> Result<(), EditError> {
    if value.trim().is_empty() {
        return Err(EditError::EmptyValue(field));
    }
    match value.starts_with('-') {
        true => Err(EditError::DashValue(field, value.to_string())),
        false => Ok(()),
    }
}

/// Build the `stencil --script <file> [-i <input>]` argv. A script's writes are its own
/// `@save` targets, which `confine::confine_dir` fences into the sandbox root.
pub fn build_script_argv(params: &ScriptParams, script_file: &str) -> Result<Argv, EditError> {
    params.validate()?;

    let mut b = ArgvBuilder::new();
    b.opt(FLAG_SCRIPT, script_file.to_string());
    if let Some(input) = &params.input {
        b.opt(FLAG_INPUT, input.to_string());
    }
    Ok(b.into_argv())
}
