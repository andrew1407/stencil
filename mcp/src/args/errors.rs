//! Errors.
//!
//! A hand-written error type whose `Display` is the exact user-facing message. Runtime
//! failures threaded up from the pipeline ride the `Runtime` variant with a ready message.

/// Everything `stencil_edit` can fail with, from parameter validation through the CLI run.
#[derive(Debug)]
pub enum EditError {
    /// Both `input` and `blank` were given.
    SourceConflict,
    /// Neither `input` nor `blank` (nor `server` + `input`) was given.
    NoSource,
    /// `source_site` (scrape mode) was empty.
    EmptySourceSite,
    /// Both `script_text` and `script_path` were given.
    ScriptSourceConflict,
    /// Neither `script_text` nor `script_path` carried a script.
    NoScript,
    /// `script_text` was larger than `MAX_SCRIPT_BYTES`.
    ScriptTooLarge(usize),
    /// `script_path` did not name a `.stc` file.
    ScriptNotStc(String),
    /// A named field was empty.
    EmptyValue(&'static str),
    /// A named field began with `-` and would misparse as a CLI flag.
    DashValue(&'static str, String),
    /// `server` was combined with `blank`.
    ServerWithBlank,
    /// `server` was given without an `input` project name.
    ServerNeedsInput,
    /// `remote_update` was set without `server`.
    RemoteUpdateWithoutServer,
    /// `remote_name` was set without `remote`.
    RemoteNameWithoutRemote,
    /// `blank.page` was combined with explicit `blank.width`/`blank.height`.
    BlankPageAndDims,
    /// `blank.page` named a page format the core doesn't know.
    UnknownPageFormat(String),
    /// Only one of `blank.width`/`blank.height` was given.
    BlankHalfDims,
    /// `blank.color` was not a color the core's `parseColor` accepts.
    UnknownColor(String),
    /// A runtime failure surfaced with an already-formatted message.
    Runtime(String),
}

impl std::fmt::Display for EditError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            EditError::SourceConflict => {
                f.write_str("`input` and `blank` are mutually exclusive — pass only one")
            }
            EditError::NoSource => f.write_str(
                "no source — pass `input` (a path/URL), `blank`, or `server` + `input`",
            ),
            EditError::EmptySourceSite => {
                f.write_str("`source_site` must not be empty — pass the http(s) URL of the page to scrape")
            }
            EditError::ScriptSourceConflict => f.write_str(
                "`script_text` and `script_path` are mutually exclusive — pass only one",
            ),
            EditError::NoScript => f.write_str(
                "no script — pass `script_text` (the .stc source) or `script_path` (a .stc file)",
            ),
            EditError::ScriptTooLarge(bytes) => write!(
                f,
                "`script_text` is {bytes} bytes — the limit is {} bytes; \
                 write it to a .stc file and pass `script_path` instead",
                crate::args::MAX_SCRIPT_BYTES
            ),
            EditError::ScriptNotStc(path) => write!(
                f,
                "`script_path` \"{path}\" is not a .stc file — Stencil scripts carry the \
                 `.stc` extension"
            ),
            EditError::EmptyValue(field) => write!(f, "`{field}` must not be empty"),
            EditError::DashValue(field, value) => {
                // The positional `output` names what a dash-leading value would cost.
                let tail = match *field {
                    "output" => ", not the output path",
                    _ => "",
                };
                write!(
                    f,
                    "`{field}` must not start with '-' (got \"{value}\") — a dash-leading \
                     value would be parsed as a CLI flag{tail}"
                )
            }
            EditError::ServerWithBlank => f.write_str(
                "`server` fetches a project as the source — it can't be combined with `blank`",
            ),
            EditError::ServerNeedsInput => {
                f.write_str("`server` needs `input` set to the name of the project to fetch")
            }
            EditError::RemoteUpdateWithoutServer => f.write_str(
                "`remote_update` writes back to a fetched project — it needs `server` (and `input`)",
            ),
            EditError::RemoteNameWithoutRemote => f.write_str(
                "`remote_name` names a `remote` upload — set `remote` (a server URL) too",
            ),
            EditError::BlankPageAndDims => f.write_str(
                "`blank.page` and `blank.width`/`blank.height` are mutually exclusive — \
                 name a page format or give pixel dims, not both",
            ),
            EditError::UnknownPageFormat(page) => write!(
                f,
                "`blank.page` \"{page}\" is not a known page format — use an ISO name \
                 (A0–A10, B0–B10, C0–C10, case-insensitive)"
            ),
            EditError::BlankHalfDims => f.write_str(
                "`blank.width` and `blank.height` must be given together (or omit both for A4)",
            ),
            EditError::UnknownColor(color) => write!(
                f,
                "`blank.color` \"{color}\" is not a recognized color — use a CSS color \
                 name, `transparent`, or `#hex` (3/4/6/8 hex digits)"
            ),
            EditError::Runtime(message) => f.write_str(message),
        }
    }
}

impl std::error::Error for EditError {}

/// Runtime failures (locate/spawn/CLI-reported errors) arrive as ready-made strings; wrap
/// them so `?` composes on `Result<_, String>` helpers.
impl From<String> for EditError {
    fn from(message: String) -> Self {
        EditError::Runtime(message)
    }
}

