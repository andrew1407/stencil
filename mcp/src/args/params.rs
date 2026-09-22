//! The tool parameter DTOs: what a caller sends, as `schemars` sees it.

use schemars::JsonSchema;
use serde::Deserialize;

use crate::config::{parse_surfaces, Surface};
use crate::layout::Layout;

/// Parameters for the `stencil_edit` tool — one transform of one image/video to one file.
#[derive(Debug, Deserialize, JsonSchema)]
pub struct EditParams {
    /// Image or video source: a local path or an `http(s)://` URL. Mutually exclusive
    /// with `blank`.
    #[serde(default)]
    pub input: Option<String>,

    /// Create a blank canvas instead of loading a source. Mutually exclusive with `input`.
    #[serde(default)]
    pub blank: Option<Blank>,

    /// Video frame index to grab (0-based). Only meaningful for video input; requires
    /// `ffmpeg` on `PATH`.
    #[serde(default)]
    pub frame: Option<u32>,

    /// Crop, as a raw spec string (`"x1=10% x2=90%"`) or an object of edges. Each edge is a
    /// length token — `px`, `cm`, `mm`, `in`, `%`, bare pixels; a leading `-` is the far edge.
    #[serde(default)]
    pub crop: Option<Crop>,

    /// On a single-axis crop, derive the missing axis from the page proportion (landscape).
    #[serde(default)]
    pub album: Option<bool>,

    /// Rotate by this many quarter-turns clockwise (negative = counter-clockwise). Only
    /// quarter-turns are supported (`int × 90°`).
    #[serde(default)]
    pub rotate: Option<i32>,

    /// Layout to draw onto the image: a path/URL string, or an inline layout object.
    #[serde(default)]
    pub layout: Option<LayoutArg>,

    /// Which frame the layout's coordinates are in (CLI `--layout-frame`): `"current"` or
    /// `"source"`. Set by the op-plan executor; plan coordinates are snapshot-frame (§1).
    #[serde(skip)]
    pub layout_frame: Option<String>,

    /// Sandbox root this run must write inside (CLI `--confine-output`). Not part of the
    /// tool schema — set by the op-plan executor, whose paths are already sandboxed.
    #[serde(skip)]
    pub confine_root: Option<String>,

    /// Image filter: `bw`, `sepia`, `invert`, `contour`, or a CSS color / `#hex` for a
    /// duotone tint. Overrides any filter baked into the layout.
    #[serde(default)]
    pub filter: Option<String>,

    /// Output file path. A missing or unknown extension is auto-filled from the input
    /// format (`png`/`jpg`/`bmp`/`tga`).
    pub output: String,

    /// Overwrite the output file if it already exists. Defaults to false (the server
    /// refuses to clobber a file you didn't intend to replace).
    #[serde(default)]
    pub overwrite: bool,

    /// Delivery surface(s) for this call — `cli`, `desktop`, `browser`, `browser-live` /
    /// `extension` — one value or a list. Omitted, the configured default is used.
    #[serde(default)]
    pub surface: Option<SurfaceArg>,

    // ── Collaboration server (server/) ──

    /// Connect to a collaboration server at this `http(s)://` URL and treat `input` as the
    /// name of a project on it. Requires `input`; incompatible with `blank`.
    #[serde(default)]
    pub server: Option<String>,

    /// With `server`, write the edited result back into the fetched project (updating its
    /// stored result image). Requires `server` (and therefore `input`).
    #[serde(default)]
    pub remote_update: Option<bool>,

    /// Upload the result as a NEW project on the collaboration server at this `http(s)://`
    /// URL. Works with any source; a web `input`'s URL is recorded as the project's source.
    #[serde(default)]
    pub remote: Option<String>,

    /// Name for the `remote` project (defaults to the input image's base name). Ignored
    /// without `remote`.
    #[serde(default)]
    pub remote_name: Option<String>,
}

/// A per-call surface override: one token or a list.
#[derive(Debug, Deserialize, JsonSchema)]
#[serde(untagged)]
pub enum SurfaceArg {
    One(String),
    Many(Vec<String>),
}

impl EditParams {
    /// Resolve the delivery surfaces for this call, falling back to `default` when the call
    /// didn't specify any.
    pub fn resolve_surfaces(&self, default: &[Surface]) -> Result<Vec<Surface>, String> {
        match &self.surface {
            None => Ok(default.to_vec()),
            Some(SurfaceArg::One(token)) => parse_surfaces(token),
            Some(SurfaceArg::Many(list)) => parse_surfaces(&list.join(",")),
        }
    }
}

/// A blank-canvas spec. Provide `width` and `height` together, or a `page` format name,
/// or omit all of them for A4 @ 96dpi.
#[derive(Debug, Deserialize, JsonSchema)]
pub struct Blank {
    /// ISO page format name (`A0`–`A10`, `B0`–`B10`, `C0`–`C10`; case-insensitive).
    /// Defaults to A4 @ 96dpi. Mutually exclusive with `width`/`height`.
    #[serde(default)]
    pub page: Option<String>,
    #[serde(default)]
    pub width: Option<u32>,
    #[serde(default)]
    pub height: Option<u32>,
    /// Fill color: a CSS name or `#hex`. Defaults to white.
    #[serde(default)]
    pub color: Option<String>,
}

/// Crop given either as a ready-made spec string or as structured edges.
#[derive(Debug, Deserialize, JsonSchema)]
#[serde(untagged)]
pub enum Crop {
    Spec(String),
    Edges {
        #[serde(default)]
        x1: Option<String>,
        #[serde(default)]
        x2: Option<String>,
        #[serde(default)]
        y1: Option<String>,
        #[serde(default)]
        y2: Option<String>,
    },
}

impl Crop {
    /// Render to the `-c` spec string the CLI expects.
    pub fn to_spec(&self) -> String {
        match self {
            Crop::Spec(s) => s.trim().to_string(),
            Crop::Edges { x1, x2, y1, y2 } => [("x1", x1), ("x2", x2), ("y1", y1), ("y2", y2)]
                .iter()
                .filter_map(|(name, edge)| edge.as_ref().map(|v| format!("{name}={v}")))
                .collect::<Vec<_>>()
                .join(" "),
        }
    }
}

/// A layout argument: a path/URL the CLI reads, or an inline layout object the server
/// materializes to a temp file.
#[derive(Debug, Deserialize, JsonSchema)]
#[serde(untagged)]
pub enum LayoutArg {
    Path(String),
    Inline(Layout),
}

/// Parameters for the `stencil_probe` tool — read an image's pixel dimensions.
#[derive(Debug, Deserialize, JsonSchema)]
pub struct ProbeParams {
    /// Image source: a local path or an `http(s)://` URL.
    pub input: String,
}

/// Parameters for the `stencil_prompt` tool — hand a natural-language request to the
/// configured LLM (`llm-contract.md`) and execute the op-plan it returns.
#[derive(Debug, Deserialize, JsonSchema)]
pub struct PromptParams {
    /// The user's instruction or question, e.g. "rotate it right and give me a sepia and
    /// a b&w variant".
    pub prompt: String,

    /// Working image: a local path or an `http(s)://` URL. A local file is also attached to
    /// the LLM for vision (≤ 8 MiB; png/jpg/webp/gif). Omit for chat-only questions.
    #[serde(default)]
    pub input: Option<String>,

    /// Directory the results are written into (created if missing): `result.png` for the
    /// plan's base actions plus one `{label}.png` per variant.
    pub output_dir: String,

    /// Override the configured `STENCIL_LLM_MODEL` for this call. The provider and endpoint
    /// are NOT overridable — they are operator config, so a caller cannot redirect the key.
    #[serde(default)]
    pub model: Option<String>,
}
