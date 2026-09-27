//! The script tools that do not run a script: `stencil_script_check`, `stencil_script_plan`
//! and `stencil_script_emit` — their parameters and argv (`cli/CONTRACT.md` §4.1, §4.3, §4.4).

use schemars::JsonSchema;
use serde::Deserialize;

use super::argv::{Argv, ArgvBuilder};
use super::errors::EditError;
use super::flags::{
    FLAG_INPUT, FLAG_NO_CLOBBER, FLAG_SCRIPT, FLAG_SCRIPT_CHECK, FLAG_SCRIPT_EMIT, FLAG_SCRIPT_PLAN,
};
use super::script::{dash_free, validate_source};

/// The script a call names: inline `.stc` text or a `.stc` path, exactly one.
#[derive(Debug, Default, Deserialize, JsonSchema)]
pub struct ScriptSource {
    /// The script as `.stc` source text (at most 256 KiB). Mutually exclusive with
    /// `script_path`.
    #[serde(default)]
    pub script_text: Option<String>,

    /// Path to an existing `.stc` file. Mutually exclusive with `script_text`.
    #[serde(default)]
    pub script_path: Option<String>,
}

impl ScriptSource {
    pub fn validate(&self) -> Result<(), EditError> {
        validate_source(&self.script_text, &self.script_path)
    }
}

/// Parameters for `stencil_script_check` — the script's diagnostics, nothing run.
#[derive(Debug, Default, Deserialize, JsonSchema)]
pub struct ScriptCheckParams {
    #[serde(flatten)]
    pub source: ScriptSource,
}

/// Parameters for `stencil_script_plan` — the script lowered to op-plans, nothing run.
#[derive(Debug, Default, Deserialize, JsonSchema)]
pub struct ScriptPlanParams {
    #[serde(flatten)]
    pub source: ScriptSource,

    /// Working image for a script with no `@source` block (a path or `http(s)://` URL); its
    /// header sizes the plan's shape ops.
    #[serde(default)]
    pub input: Option<String>,
}

/// Parameters for `stencil_script_emit` — the script rewritten for another surface.
#[derive(Debug, Default, Deserialize, JsonSchema)]
pub struct ScriptEmitParams {
    #[serde(flatten)]
    pub source: ScriptSource,

    /// The file to write, inside the server's roots. Its extension picks the target:
    /// `.js`/`.stcjs` for the browser's `window.stencil` facade, `.py`/`.pystc` for pystencil.
    pub output: String,

    /// Replace an existing `output`. Defaults to false.
    #[serde(default)]
    pub overwrite: bool,
}

/// `stencil --script-check <file>`: diagnostics on stdout, exit 1 on any error.
pub fn build_check_argv(params: &ScriptCheckParams, file: &str) -> Result<Argv, EditError> {
    params.source.validate()?;
    let mut b = ArgvBuilder::new();
    b.opt(FLAG_SCRIPT_CHECK, file.to_string());
    Ok(b.into_argv())
}

/// `stencil --script-plan <file> [-i <input>]`: one JSON envelope on stdout.
pub fn build_plan_argv(params: &ScriptPlanParams, file: &str) -> Result<Argv, EditError> {
    params.source.validate()?;
    let mut b = ArgvBuilder::new();
    b.opt(FLAG_SCRIPT_PLAN, file.to_string());
    if let Some(input) = &params.input {
        dash_free("input", input)?;
        b.opt(FLAG_INPUT, input.to_string());
    }
    Ok(b.into_argv())
}

/// `stencil --script <file> [--no-clobber] --script-emit <out>`; `out` stays the LAST token, and
/// the CLI owns the clobber check (`cli/CONTRACT.md`, `--no-clobber`).
pub fn build_emit_argv(params: &ScriptEmitParams, file: &str) -> Result<Argv, EditError> {
    params.source.validate()?;
    dash_free("output", &params.output)?;
    let mut b = ArgvBuilder::new();
    b.opt(FLAG_SCRIPT, file.to_string());
    b.switch_if(FLAG_NO_CLOBBER, !params.overwrite);
    b.opt(FLAG_SCRIPT_EMIT, params.output.clone());
    Ok(b.into_argv())
}
