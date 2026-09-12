//! The `source_site` tool: its parameters, its surface guard, and its argv.

use schemars::JsonSchema;
use serde::Deserialize;

use super::argv::{Argv, ArgvBuilder};
use super::errors::EditError;
use super::params::SurfaceArg;
use crate::config::{parse_surfaces, Surface};

/// Parameters for the `source_site` tool — scrape a web page, download the media that
/// matches the category/format/dimension filters into a directory. Mirrors the CLI's
/// `--source-site` scrape mode (`cli/src/scrape.zig`); the CLI (not the server) fetches the
/// page and parses its HTML.
#[derive(Debug, Deserialize, JsonSchema)]
pub struct ScrapeParams {
    /// The page to scrape: an `http(s)://` URL. Its HTML is fetched and scanned for
    /// image/video/background/poster media.
    pub source_site: String,

    /// Destination **directory** for the downloads (created if missing, nested ok).
    /// Defaults to the current directory.
    #[serde(default)]
    pub output: Option<String>,

    /// Items per page/group. Omitted ⇒ the CLI's default of **5**; `0` ⇒ **all** matches
    /// (and `group` is ignored).
    #[serde(default)]
    pub count: Option<u32>,

    /// 0-based page index over the filtered list; the window is `filtered[group*count ..
    /// group*count+count]`. Only meaningful with `count`. Defaults to 0.
    #[serde(default)]
    pub group: Option<u32>,

    /// Category tokens, `|`-separated: any of `img`, `video`, `background`, `poster`.
    /// Omitted (or `all`) = every category.
    #[serde(default)]
    pub filter: Option<String>,

    /// Format tokens, `|`-separated normalized extensions (e.g. `png|jpg|webp|mp4`).
    /// Omitted (or `all`) = every format.
    #[serde(default)]
    pub format: Option<String>,

    /// Regex (POSIX ERE, case-insensitive; substring on a Windows CLI build) to match against
    /// each media URL. Omitted = every URL. Passed through as `--source-name`.
    #[serde(default)]
    pub name: Option<String>,

    /// Inclusive minimum width in px for image-category items (measured from the bytes);
    /// `0`/omitted = unset. Unmeasurable items (video) always pass.
    #[serde(default)]
    pub min_width: Option<u32>,
    /// Inclusive maximum width in px; `0`/omitted = unset.
    #[serde(default)]
    pub max_width: Option<u32>,
    /// Inclusive minimum height in px; `0`/omitted = unset.
    #[serde(default)]
    pub min_height: Option<u32>,
    /// Inclusive maximum height in px; `0`/omitted = unset.
    #[serde(default)]
    pub max_height: Option<u32>,

    /// Delivery surface. Scraping only writes files locally, so this may only be `cli`
    /// (the default). Any other surface is rejected.
    #[serde(default)]
    pub surface: Option<SurfaceArg>,
}

impl ScrapeParams {
    /// Scraping writes a **directory** of downloaded files — the only delivery it supports is
    /// the local `cli` file write. Accept an explicit `cli` (or nothing) and reject any other
    /// surface up front, before the CLI runs.
    pub fn validate_surface(&self) -> Result<(), String> {
        let surfaces = match &self.surface {
            None => return Ok(()),
            Some(SurfaceArg::One(token)) => parse_surfaces(token)?,
            Some(SurfaceArg::Many(list)) => parse_surfaces(&list.join(","))?,
        };
        if surfaces.as_slice() != [Surface::Cli] {
            return Err(
                "source_site only supports the `cli` surface — it writes a directory of \
                 downloaded files, not a single deliverable image"
                    .to_string(),
            );
        }
        Ok(())
    }
}

// Scrape mode (`--source-site`). These mirror the CLI's `cli/src/args.zig` scrape flags.
const FLAG_SOURCE_SITE: &str = "--source-site";
const FLAG_SOURCE_COUNT: &str = "--source-count";
const FLAG_GROUP: &str = "--group";
const FLAG_SOURCE_FILTER: &str = "--source-filter";
const FLAG_SOURCE_FORMAT: &str = "--source-format";
const FLAG_SOURCE_NAME: &str = "--source-name";
const FLAG_SOURCE_MIN_WIDTH: &str = "--source-min-width";
const FLAG_SOURCE_MAX_WIDTH: &str = "--source-max-width";
const FLAG_SOURCE_MIN_HEIGHT: &str = "--source-min-height";
const FLAG_SOURCE_MAX_HEIGHT: &str = "--source-max-height";

/// Build the `stencil --source-site …` scrape argv. The flag layout mirrors §1 of the design
/// contract (and `cli/src/args.zig`); the CLI parses flags order-independently. The positional
/// `output` directory rides last and is guarded against a leading dash for the same
/// flag-injection reason as `build_argv` (the CLI has no `--` terminator).
pub fn build_scrape_argv(params: &ScrapeParams) -> Result<Argv, EditError> {
    if params.source_site.trim().is_empty() {
        return Err(EditError::EmptySourceSite);
    }

    // The destination directory defaults to the current directory, matching the CLI's own
    // positional default. An empty or dash-leading directory is rejected up front.
    let output = params.output.as_deref().unwrap_or(".");
    if output.trim().is_empty() {
        return Err(EditError::EmptyOutput);
    }
    if output.starts_with('-') {
        return Err(EditError::DashOutput(output.to_string()));
    }

    let mut b = ArgvBuilder::new();
    b.opt(FLAG_SOURCE_SITE, params.source_site.clone());
    if let Some(count) = params.count {
        b.opt(FLAG_SOURCE_COUNT, count.to_string());
    }
    if let Some(group) = params.group {
        b.opt(FLAG_GROUP, group.to_string());
    }
    if let Some(filter) = &params.filter {
        b.opt(FLAG_SOURCE_FILTER, filter.clone());
    }
    if let Some(format) = &params.format {
        b.opt(FLAG_SOURCE_FORMAT, format.clone());
    }
    if let Some(name) = &params.name {
        b.opt(FLAG_SOURCE_NAME, name.clone());
    }
    if let Some(v) = params.min_width {
        b.opt(FLAG_SOURCE_MIN_WIDTH, v.to_string());
    }
    if let Some(v) = params.max_width {
        b.opt(FLAG_SOURCE_MAX_WIDTH, v.to_string());
    }
    if let Some(v) = params.min_height {
        b.opt(FLAG_SOURCE_MIN_HEIGHT, v.to_string());
    }
    if let Some(v) = params.max_height {
        b.opt(FLAG_SOURCE_MAX_HEIGHT, v.to_string());
    }
    b.arg(output.to_string());
    Ok(b.into_argv())
}
