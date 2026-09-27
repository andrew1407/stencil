//! The project writes (`cli/CONTRACT.md` §6.3–§6.5): `stencil_project_update`'s fields as the
//! CLI's `--project-update` + `--set-*` flags, a project's stored files as `--project-files`,
//! and one of them downloaded with `--project-file`. Every id is the server's own shape.

use schemars::JsonSchema;
use serde::Deserialize;

use super::argv::{Argv, ArgvBuilder};
use super::errors::EditError;
use super::flags::{
    FLAG_IF_VERSION, FLAG_NO_CLOBBER, FLAG_PROJECT_FILE, FLAG_PROJECT_FILES, FLAG_PROJECT_UPDATE,
    FLAG_SERVER, FLAG_SET_BLANK_COLOR, FLAG_SET_COLOR, FLAG_SET_DESCRIPTION, FLAG_SET_EXPIRES,
    FLAG_SET_KEYWORDS, FLAG_SET_NAME,
};
use super::script::dash_free;

/// Parameters for the `stencil_project_update` tool — change one project's metadata.
#[derive(Debug, Default, Deserialize, JsonSchema)]
pub struct ProjectUpdateParams {
    /// The server: one of the operator's allowlisted origins. May be omitted when exactly one
    /// server is allowed.
    #[serde(default)]
    pub server: Option<String>,

    /// The project's id (`p_…`), as `stencil_projects` lists it.
    pub id: String,

    /// A new name; it cannot be blank.
    #[serde(default)]
    pub name: Option<String>,

    /// A new description; "" clears it.
    #[serde(default)]
    pub description: Option<String>,

    /// Keywords replacing the current ones (no commas inside one); [] clears them.
    #[serde(default)]
    pub keywords: Option<Vec<String>>,

    /// The name's colour: `#rrggbb` or a colour name; "" clears it.
    #[serde(default)]
    pub color: Option<String>,

    /// A blank project's fill colour: `#rrggbb` or a colour name; "" makes it not blank.
    #[serde(default)]
    pub blank_color: Option<String>,

    /// When the project expires, in epoch milliseconds; 0 keeps it forever.
    #[serde(default)]
    pub expires_at: Option<u64>,

    /// Refuse the change once the project has moved past this version (the `version`
    /// `stencil_projects` reported). Omitted, the project's current version guards it.
    #[serde(default)]
    pub if_version: Option<u64>,
}

/// Which stored file of a project (the server's file kinds).
#[derive(Debug, Clone, Copy, Deserialize, JsonSchema)]
#[serde(rename_all = "lowercase")]
pub enum FileKind {
    Original,
    Result,
    Video,
    Chat,
    Variant1,
    Variant2,
    Variant3,
    Variant4,
    Variant5,
    Variant6,
    Variant7,
    Variant8,
}

impl FileKind {
    pub fn as_str(self) -> &'static str {
        const NAMES: [&str; 12] = [
            "original", "result", "video", "chat", "variant1", "variant2", "variant3",
            "variant4", "variant5", "variant6", "variant7", "variant8",
        ];
        NAMES[self as usize]
    }
}

/// Parameters for the `stencil_project_file` tool — download one file a project stores.
#[derive(Debug, Deserialize, JsonSchema)]
pub struct ProjectFileParams {
    /// The server: one of the operator's allowlisted origins. May be omitted when exactly one
    /// server is allowed.
    #[serde(default)]
    pub server: Option<String>,

    /// The project's id (`p_…`), as `stencil_projects` lists it.
    pub id: String,

    /// Which stored file: `original`, `result`, `video`, `chat` or `variant1`…`variant8`.
    pub kind: FileKind,

    /// Where to write it, inside the server's roots (a relative path resolves against the
    /// first). The bytes are written as served, so give it the extension of the file's format.
    pub output: String,

    /// Replace an existing file at `output`. Defaults to false.
    #[serde(default)]
    pub overwrite: bool,
}

/// The server's project id shape, `p_<base36>_<base36>`: letters, digits and `_` only.
pub fn check_project_id(id: &str) -> Result<(), EditError> {
    let safe = !id.is_empty() && id.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_');
    match safe {
        true => Ok(()),
        false => Err(EditError::Refused(format!("`id` \"{id}\" is not a project id"))),
    }
}

/// `stencil --server <origin> --project-update <id>` and one `--set-*` flag per field given.
pub fn build_project_update_argv(
    origin: &str,
    params: &ProjectUpdateParams,
) -> Result<Argv, EditError> {
    dash_free("server", origin)?;
    check_project_id(&params.id)?;
    if params.name.as_deref().is_some_and(|n| n.trim().is_empty()) {
        return Err(EditError::Refused("`name` must not be blank".into()));
    }
    let keywords = match &params.keywords {
        Some(list) if list.iter().any(|k| k.contains(',') || k.trim().is_empty()) => {
            let why = "each of `keywords` must be a non-blank word without a comma";
            return Err(EditError::Refused(why.into()));
        }
        Some(list) => Some(list.join(",")),
        None => None,
    };
    let mut b = ArgvBuilder::new();
    b.opt(FLAG_SERVER, origin.to_string());
    b.opt(FLAG_PROJECT_UPDATE, params.id.clone());
    let text = [
        (FLAG_SET_NAME, params.name.clone()),
        (FLAG_SET_DESCRIPTION, params.description.clone()),
        (FLAG_SET_KEYWORDS, keywords),
        (FLAG_SET_COLOR, params.color.clone()),
        (FLAG_SET_BLANK_COLOR, params.blank_color.clone()),
        (FLAG_SET_EXPIRES, params.expires_at.map(|ms| ms.to_string())),
    ];
    let mut fields = 0;
    for (flag, value) in text {
        if let Some(value) = value {
            b.opt(flag, value);
            fields += 1;
        }
    }
    if fields == 0 {
        return Err(EditError::Refused(
            "nothing to change — pass `name`, `description`, `keywords`, `color`, \
             `blank_color` or `expires_at`"
                .into(),
        ));
    }
    if let Some(version) = params.if_version {
        b.opt(FLAG_IF_VERSION, version.to_string());
    }
    Ok(b.into_argv())
}

/// `stencil --server <origin> --project-files <id>`: the metadata plus each stored file.
pub fn build_project_files_argv(origin: &str, id: &str) -> Result<Argv, EditError> {
    dash_free("server", origin)?;
    check_project_id(id)?;
    let mut b = ArgvBuilder::new();
    b.opt(FLAG_SERVER, origin.to_string());
    b.opt(FLAG_PROJECT_FILES, id.to_string());
    Ok(b.into_argv())
}

/// `stencil --server <origin> --project-file <id> <kind> [--no-clobber] <output>`, the output
/// last so the run can be confined like an edit's.
pub fn build_project_file_argv(
    origin: &str,
    params: &ProjectFileParams,
    output: &str,
) -> Result<Argv, EditError> {
    dash_free("server", origin)?;
    check_project_id(&params.id)?;
    dash_free("output", output)?;
    let mut b = ArgvBuilder::new();
    b.opt(FLAG_SERVER, origin.to_string());
    b.opt(FLAG_PROJECT_FILE, params.id.clone());
    b.arg(params.kind.as_str());
    b.switch_if(FLAG_NO_CLOBBER, !params.overwrite);
    b.arg(output.to_string());
    Ok(b.into_argv())
}
