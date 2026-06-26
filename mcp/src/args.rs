//! Typed tool parameters and their translation into the CLI's argv.
//!
//! This mirrors the role of `cli/src/args.zig`: it owns the mapping between a request and
//! the exact `stencil [options] <output>` command line. The pipeline order is fixed by the
//! CLI itself (source → crop → rotate → layout → filter → encode), so argv order here is
//! only cosmetic; the CLI parses flags order-independently.

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

    /// Crop, as either a raw spec string (`"x1=10% x2=90% y1=10% y2=90%"`) or an object of
    /// edges. Each edge is a length token: `px`, `cm`, `mm`, `in`, `%`, or a bare pixel
    /// delta; a leading `-` measures from the far edge. Omit an edge to keep the image
    /// bound.
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

    /// Image filter: `bw`, `sepia`, or a CSS color / `#hex` for a duotone tint. Overrides
    /// any filter baked into the layout.
    #[serde(default)]
    pub filter: Option<String>,

    /// Output file path. A missing or unknown extension is auto-filled from the input
    /// format (`png`/`jpg`/`bmp`/`tga`).
    pub output: String,

    /// Overwrite the output file if it already exists. Defaults to false (the server
    /// refuses to clobber a file you didn't intend to replace).
    #[serde(default)]
    pub overwrite: bool,

    /// Override the default delivery surface(s) for this call — where the result is
    /// presented. A single value (`"browser"`) or a list (`["cli", "desktop"]`). Known
    /// surfaces: `cli` (write the file), `desktop` (launch the Qt app), `browser` (editor
    /// launch URL), `browser-live` / `extension` (delegated to the stencil-operator agent).
    /// When omitted, the server's configured default is used.
    #[serde(default)]
    pub surface: Option<SurfaceArg>,

    // ── Collaboration server (server/) ──
    // These drive the CLI's server client: connect over REST, fetch/create projects, and
    // upload result bytes. The Stencil collaboration server stores/shares projects across
    // all front-ends; see ../server/README.md for the wire protocol.
    /// Connect to a collaboration server at this `http(s)://` URL and treat `input` as the
    /// **name of a project on that server**: the project's image is fetched and edited
    /// instead of a local file. Requires `input` (the project name); incompatible with
    /// `blank`. Pair with `remote_update` to write the result back into that project.
    #[serde(default)]
    pub server: Option<String>,

    /// With `server`, write the edited result back into the fetched project (updating its
    /// stored result image). Requires `server` (and therefore `input`).
    #[serde(default)]
    pub remote_update: Option<bool>,

    /// Upload the result as a **new** project on the collaboration server at this
    /// `http(s)://` URL. Works with any source — a local/web `input`, a `blank`, or a
    /// `server`-fetched project. A web `input`'s URL is recorded as the project's source.
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

/// A blank-canvas spec. Provide `width` and `height` together, or omit both for A4 @ 96dpi.
#[derive(Debug, Deserialize, JsonSchema)]
pub struct Blank {
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

/// Build the `stencil` argv from edit parameters. `layout_path` is the already-resolved
/// path passed to `-l` (a temp file for an inline layout, or the user's path/URL); pass
/// `None` to omit `--layout`. Validates the source and blank-dimension invariants the CLI
/// would otherwise reject with a terse message.
pub fn build_argv(params: &EditParams, layout_path: Option<&str>) -> Result<Vec<String>, String> {
    if params.input.is_some() && params.blank.is_some() {
        return Err("`input` and `blank` are mutually exclusive — pass only one".into());
    }
    if params.input.is_none() && params.blank.is_none() {
        return Err("no source — pass `input` (a path/URL), `blank`, or `server` + `input`".into());
    }
    if params.output.trim().is_empty() {
        return Err("`output` must not be empty".into());
    }

    // Collaboration-server invariants, mirroring the CLI's own checks.
    if params.server.is_some() {
        if params.blank.is_some() {
            return Err(
                "`server` fetches a project as the source — it can't be combined with `blank`"
                    .into(),
            );
        }
        if params.input.is_none() {
            return Err("`server` needs `input` set to the name of the project to fetch".into());
        }
    }
    if params.remote_update.unwrap_or(false) && params.server.is_none() {
        return Err(
            "`remote_update` writes back to a fetched project — it needs `server` (and `input`)"
                .into(),
        );
    }
    if params.remote_name.is_some() && params.remote.is_none() {
        return Err("`remote_name` names a `remote` upload — set `remote` (a server URL) too".into());
    }

    let mut argv: Vec<String> = Vec::new();

    // `--server <url>` must precede `-i` conceptually (it changes what `-i` means), but the
    // CLI parses order-independently; keep them adjacent for readability.
    if let Some(server) = &params.server {
        argv.push("--server".into());
        argv.push(server.clone());
    }

    if let Some(input) = &params.input {
        argv.push("-i".into());
        argv.push(input.clone());
    }

    if let Some(blank) = &params.blank {
        argv.push("--blank".into());
        match (blank.width, blank.height) {
            (Some(w), Some(h)) => {
                argv.push(w.to_string());
                argv.push(h.to_string());
            }
            (None, None) => {}
            _ => {
                return Err(
                    "`blank.width` and `blank.height` must be given together (or omit both for A4)"
                        .into(),
                )
            }
        }
        if let Some(color) = &blank.color {
            argv.push(color.clone());
        }
    }

    if let Some(frame) = params.frame {
        argv.push("-f".into());
        argv.push(frame.to_string());
    }

    if let Some(crop) = &params.crop {
        let spec = crop.to_spec();
        if !spec.is_empty() {
            argv.push("-c".into());
            argv.push(spec);
        }
    }

    if params.album.unwrap_or(false) {
        argv.push("--album".into());
    }

    if let Some(rotate) = params.rotate {
        argv.push("-r".into());
        argv.push(rotate.to_string());
    }

    if let Some(path) = layout_path {
        argv.push("-l".into());
        argv.push(path.to_string());
    }

    if let Some(filter) = &params.filter {
        argv.push("--filter".into());
        argv.push(filter.clone());
    }

    // Server delivery: write the result back into the fetched project, and/or push it as a
    // new project. The result is always saved locally too (the positional output below).
    if params.remote_update.unwrap_or(false) {
        argv.push("--remote-update".into());
    }
    if let Some(remote) = &params.remote {
        argv.push("--remote".into());
        argv.push(remote.clone());
    }
    if let Some(name) = &params.remote_name {
        argv.push("--remote-name".into());
        argv.push(name.clone());
    }

    argv.push(params.output.clone());
    Ok(argv)
}
