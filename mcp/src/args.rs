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

/// The ISO page-format names the CLI's core recognizes: `A0`–`A10`, `B0`–`B10`,
/// `C0`–`C10` (ISO 216 A/B + ISO 269 C series), matched case-insensitively. Mirrors
/// `canonicalPageFormat` in `cli/src/core.zig` / `pageFormatNames` in
/// `core/page/pageMetrics.cpp`.
const PAGE_FORMATS: [&str; 33] = [
    "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "A10", //
    "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "B9", "B10", //
    "C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "C10",
];

/// Whether `name` is a known page-format token (case-insensitive). The CLI's `--blank`
/// parser silently skips an unrecognized token — it would fall through to the positional
/// output slot and the blank would come out A4 with no error — so the server must reject
/// unknown names before they reach argv.
fn is_page_format(name: &str) -> bool {
    PAGE_FORMATS.iter().any(|f| f.eq_ignore_ascii_case(name))
}

/// The CSS Color Module Level 4 extended colour keywords the CLI's core recognizes.
/// Mirrors the `namedColors` table in `core/color/colorNames.cpp` (which `parseColor`
/// consults after trying `transparent` and `#hex`).
const COLOR_NAMES: [&str; 148] = [
    "aliceblue", "antiquewhite", "aqua", "aquamarine", "azure", "beige", //
    "bisque", "black", "blanchedalmond", "blue", "blueviolet", "brown", //
    "burlywood", "cadetblue", "chartreuse", "chocolate", "coral", "cornflowerblue", //
    "cornsilk", "crimson", "cyan", "darkblue", "darkcyan", "darkgoldenrod", //
    "darkgray", "darkgrey", "darkgreen", "darkkhaki", "darkmagenta", "darkolivegreen", //
    "darkorange", "darkorchid", "darkred", "darksalmon", "darkseagreen", "darkslateblue", //
    "darkslategray", "darkslategrey", "darkturquoise", "darkviolet", "deeppink", "deepskyblue", //
    "dimgray", "dimgrey", "dodgerblue", "firebrick", "floralwhite", "forestgreen", //
    "fuchsia", "gainsboro", "ghostwhite", "gold", "goldenrod", "gray", //
    "grey", "green", "greenyellow", "honeydew", "hotpink", "indianred", //
    "indigo", "ivory", "khaki", "lavender", "lavenderblush", "lawngreen", //
    "lemonchiffon", "lightblue", "lightcoral", "lightcyan", "lightgoldenrodyellow", "lightgray", //
    "lightgrey", "lightgreen", "lightpink", "lightsalmon", "lightseagreen", "lightskyblue", //
    "lightslategray", "lightslategrey", "lightsteelblue", "lightyellow", "lime", "limegreen", //
    "linen", "magenta", "maroon", "mediumaquamarine", "mediumblue", "mediumorchid", //
    "mediumpurple", "mediumseagreen", "mediumslateblue", "mediumspringgreen", "mediumturquoise",
    "mediumvioletred", //
    "midnightblue", "mintcream", "mistyrose", "moccasin", "navajowhite", "navy", //
    "oldlace", "olive", "olivedrab", "orange", "orangered", "orchid", //
    "palegoldenrod", "palegreen", "paleturquoise", "palevioletred", "papayawhip", "peachpuff", //
    "peru", "pink", "plum", "powderblue", "purple", "rebeccapurple", //
    "red", "rosybrown", "royalblue", "saddlebrown", "salmon", "sandybrown", //
    "seagreen", "seashell", "sienna", "silver", "skyblue", "slateblue", //
    "slategray", "slategrey", "snow", "springgreen", "steelblue", "tan", //
    "teal", "thistle", "tomato", "turquoise", "violet", "wheat", //
    "white", "whitesmoke", "yellow", "yellowgreen",
];

/// Whether `spec` is a colour the CLI's `parseColor` accepts (`cli/src/core.zig` →
/// `parseColor` in `core/color/colorNames.cpp`): after trimming and ASCII-lowercasing,
/// `transparent`, `#` + 3/4/6/8 hex digits, or a CSS named colour. Like an unknown page
/// token, an unparseable colour is silently skipped by the CLI's `--blank` parser — the
/// blank would come out white with no error — so the server must reject it before argv.
fn is_color(spec: &str) -> bool {
    let s = spec.trim().to_ascii_lowercase();
    if s.is_empty() {
        return false;
    }
    if s == "transparent" {
        return true;
    }
    if let Some(hex) = s.strip_prefix('#') {
        return matches!(hex.len(), 3 | 4 | 6 | 8) && hex.bytes().all(|b| b.is_ascii_hexdigit());
    }
    COLOR_NAMES.contains(&s.as_str())
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

// ── CLI flag names ──
// The exact option strings understood by the Zig CLI (`cli/src/args.zig`). Centralized here
// so the flag contract is single-sourced and greppable; `build_argv` references these instead
// of bare literals. Changing a flag string means changing it in the CLI too.
const FLAG_SERVER: &str = "--server";
const FLAG_INPUT: &str = "-i";
const FLAG_BLANK: &str = "--blank";
const FLAG_FRAME: &str = "-f";
const FLAG_CROP: &str = "-c";
const FLAG_ALBUM: &str = "--album";
const FLAG_ROTATE: &str = "-r";
const FLAG_LAYOUT: &str = "-l";
const FLAG_FILTER: &str = "--filter";
const FLAG_REMOTE_UPDATE: &str = "--remote-update";
const FLAG_REMOTE: &str = "--remote";
const FLAG_REMOTE_NAME: &str = "--remote-name";

// ── Errors ──
// A hand-written error type (no `thiserror`) whose `Display` reproduces the exact
// user-facing message for each failure, so the MCP error responses and the test suites are
// byte-for-byte unchanged. Validation cases carry structured data; runtime failures threaded
// up from the pipeline (clobber guard, layout temp write, CLI locate/spawn, CLI-reported
// `error:` lines, missing `wrote` line) ride the `Runtime` variant with a ready-made message.

/// Everything `stencil_edit` can fail with, from parameter validation through the CLI run.
#[derive(Debug)]
pub enum EditError {
    /// Both `input` and `blank` were given.
    SourceConflict,
    /// Neither `input` nor `blank` (nor `server` + `input`) was given.
    NoSource,
    /// `output` was empty.
    EmptyOutput,
    /// `output` began with `-` and would misparse as a CLI flag.
    DashOutput(String),
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
            EditError::EmptyOutput => f.write_str("`output` must not be empty"),
            EditError::DashOutput(output) => write!(
                f,
                "`output` must not start with '-' (got \"{output}\") — a dash-leading value \
                 would be parsed as a CLI flag, not the output path"
            ),
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

/// The boundary back to the plain-string surface the MCP handler presents.
impl From<EditError> for String {
    fn from(error: EditError) -> Self {
        error.to_string()
    }
}

// ── Argv assembly ──

/// A tiny push helper that collapses the repeated `argv.push(FLAG.into()); argv.push(x)`
/// pairs into one call each, so `build_argv` reads as a flat mapping. It only owns a
/// `Vec<String>`; ordering is entirely the caller's, matching the CLI's flag layout.
struct ArgvBuilder {
    argv: Vec<String>,
}

impl ArgvBuilder {
    fn new() -> Self {
        Self { argv: Vec::new() }
    }

    /// Push a flag and its value as two argv tokens.
    fn opt(&mut self, flag: &str, value: impl Into<String>) {
        self.argv.push(flag.to_string());
        self.argv.push(value.into());
    }

    /// Push a bare flag (no value).
    fn switch(&mut self, flag: &str) {
        self.argv.push(flag.to_string());
    }

    /// Push a bare flag only when `cond` holds.
    fn switch_if(&mut self, flag: &str, cond: bool) {
        if cond {
            self.switch(flag);
        }
    }

    /// Push a bare positional/value token (no flag).
    fn arg(&mut self, value: impl Into<String>) {
        self.argv.push(value.into());
    }

    fn into_argv(self) -> Vec<String> {
        self.argv
    }
}

/// The validated, normalized source of an edit: exactly one of a plain input, a blank
/// canvas, or a named project fetched from a collaboration server. `TryFrom<&EditParams>`
/// is the single normalization boundary — it runs the source/output/server/remote guards
/// once, in the CLI's own order, so the flat public `EditParams` (and its JSON schema) stay
/// untouched while `build_argv` consumes a shape that can't be inconsistent.
enum Source<'a> {
    Input(&'a str),
    Blank(&'a Blank),
    ServerProject { server: &'a str, name: &'a str },
}

impl<'a> TryFrom<&'a EditParams> for Source<'a> {
    type Error = EditError;

    fn try_from(p: &'a EditParams) -> Result<Self, EditError> {
        // Source exclusivity.
        if p.input.is_some() && p.blank.is_some() {
            return Err(EditError::SourceConflict);
        }
        if p.input.is_none() && p.blank.is_none() {
            return Err(EditError::NoSource);
        }

        // Flag-injection guard on the positional `output`. The output is appended last and
        // the CLI (`cli/src/args.zig`) has *no* `--` end-of-options terminator (a bare `--`
        // is itself rejected as an unknown flag), so an `output` like `--album` or `-l`
        // would be parsed as a flag rather than the path (`-l` would even swallow the
        // following token). A real output path never starts with a dash, so reject it up
        // front — mirroring the CLI's own `arg[0] == '-'` flag test.
        //
        // Scheme/host SSRF filtering for `input`/`server`/`remote` is deliberately NOT done
        // here: it is enforced downstream in the CLI (which was hardened for this), and this
        // builder only guarantees each value rides as a single inert argv token (no shell,
        // no splitting).
        if p.output.trim().is_empty() {
            return Err(EditError::EmptyOutput);
        }
        if p.output.starts_with('-') {
            return Err(EditError::DashOutput(p.output.clone()));
        }

        // Collaboration-server invariants, mirroring the CLI's own checks.
        if p.server.is_some() {
            if p.blank.is_some() {
                return Err(EditError::ServerWithBlank);
            }
            if p.input.is_none() {
                return Err(EditError::ServerNeedsInput);
            }
        }
        if p.remote_update.unwrap_or(false) && p.server.is_none() {
            return Err(EditError::RemoteUpdateWithoutServer);
        }
        if p.remote_name.is_some() && p.remote.is_none() {
            return Err(EditError::RemoteNameWithoutRemote);
        }

        // Normalize. The guards above guarantee `input` is Some whenever `server` is.
        Ok(if let Some(server) = p.server.as_deref() {
            Source::ServerProject {
                server,
                name: p.input.as_deref().expect("server implies input"),
            }
        } else if let Some(input) = p.input.as_deref() {
            Source::Input(input)
        } else {
            Source::Blank(p.blank.as_ref().expect("no input implies blank"))
        })
    }
}

impl Source<'_> {
    /// Emit the source's leading argv: `--server <url> -i <name>`, `-i <input>`, or the
    /// `--blank …` series. `--server <url>` conceptually precedes `-i` (it changes what
    /// `-i` means), though the CLI parses order-independently.
    fn push_argv(&self, b: &mut ArgvBuilder) -> Result<(), EditError> {
        match self {
            Source::Input(input) => b.opt(FLAG_INPUT, *input),
            Source::ServerProject { server, name } => {
                b.opt(FLAG_SERVER, *server);
                b.opt(FLAG_INPUT, *name);
            }
            Source::Blank(blank) => blank.push_argv(b)?,
        }
        Ok(())
    }
}

impl Blank {
    /// Emit `--blank [page] [w h] [color]`, validating the traps the CLI would otherwise
    /// swallow silently (an unknown page token or unparseable colour is skipped by the
    /// CLI's `--blank` parser, so it must be rejected here instead of yielding a default).
    fn push_argv(&self, b: &mut ArgvBuilder) -> Result<(), EditError> {
        b.switch(FLAG_BLANK);
        if let Some(page) = &self.page {
            // Mirrors the CLI's own rule: a format token and explicit dims can't combine.
            if self.width.is_some() || self.height.is_some() {
                return Err(EditError::BlankPageAndDims);
            }
            if !is_page_format(page) {
                return Err(EditError::UnknownPageFormat(page.clone()));
            }
            b.arg(page.clone());
        }
        match (self.width, self.height) {
            (Some(w), Some(h)) => {
                b.arg(w.to_string());
                b.arg(h.to_string());
            }
            (None, None) => {}
            _ => return Err(EditError::BlankHalfDims),
        }
        if let Some(color) = &self.color {
            if !is_color(color) {
                return Err(EditError::UnknownColor(color.clone()));
            }
            b.arg(color.clone());
        }
        Ok(())
    }
}

/// Build the `stencil` argv from edit parameters. `layout_path` is the already-resolved
/// path passed to `-l` (a temp file for an inline layout, or the user's path/URL); pass
/// `None` to omit `--layout`. All validation happens in `Source::try_from`, so the body is a
/// flat, order-fixed mapping of the remaining flags.
pub fn build_argv(
    params: &EditParams,
    layout_path: Option<&str>,
) -> Result<Vec<String>, EditError> {
    let source = Source::try_from(params)?;

    let mut b = ArgvBuilder::new();
    source.push_argv(&mut b)?;

    if let Some(frame) = params.frame {
        b.opt(FLAG_FRAME, frame.to_string());
    }
    if let Some(crop) = &params.crop {
        let spec = crop.to_spec();
        if !spec.is_empty() {
            b.opt(FLAG_CROP, spec);
        }
    }
    b.switch_if(FLAG_ALBUM, params.album.unwrap_or(false));
    if let Some(rotate) = params.rotate {
        b.opt(FLAG_ROTATE, rotate.to_string());
    }
    if let Some(path) = layout_path {
        b.opt(FLAG_LAYOUT, path);
    }
    if let Some(filter) = &params.filter {
        b.opt(FLAG_FILTER, filter.clone());
    }

    // Server delivery: write the result back into the fetched project, and/or push it as a
    // new project. The result is always saved locally too (the positional output below).
    b.switch_if(FLAG_REMOTE_UPDATE, params.remote_update.unwrap_or(false));
    if let Some(remote) = &params.remote {
        b.opt(FLAG_REMOTE, remote.clone());
    }
    if let Some(name) = &params.remote_name {
        b.opt(FLAG_REMOTE_NAME, name.clone());
    }

    b.arg(params.output.clone());
    Ok(b.into_argv())
}
