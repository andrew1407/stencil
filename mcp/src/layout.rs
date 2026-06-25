//! The layout JSON the CLI's `--layout` flag draws onto an image.
//!
//! These types mirror the schema the browser exports and the CLI parses
//! (`cli/src/layout.zig` ← `core/raster`). Coordinates are **image pixels**. A line is a
//! polyline through its `points`; repeat the first point and set a non-`transparent`
//! `fillColor` to close and fill a shape. Per-line defaults (applied by the CLI when a
//! field is omitted): color `#FFFF00`, thickness `2`, markerSize `4`, style `solid`,
//! fillColor `transparent`.

use std::io::Write;

use schemars::JsonSchema;
use serde::{Deserialize, Serialize};

/// A full layout: optional source dimensions/filter plus the lines to draw.
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct Layout {
    /// Source image width in pixels (advisory; the CLI draws against the live image).
    #[serde(rename = "imageWidth", skip_serializing_if = "Option::is_none")]
    pub image_width: Option<f64>,
    /// Source image height in pixels (advisory).
    #[serde(rename = "imageHeight", skip_serializing_if = "Option::is_none")]
    pub image_height: Option<f64>,
    /// Filter baked into the layout (`bw`/`sepia`/a color). A top-level `filter`
    /// argument to `stencil_edit` overrides this.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub filter: Option<String>,
    /// The polylines to burn into the image.
    #[serde(default)]
    pub lines: Vec<Line>,
}

/// One polyline / closed shape.
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct Line {
    /// Vertices in image-pixel space. A line with no points is skipped by the CLI.
    pub points: Vec<Point>,
    /// Stroke color (CSS name / `#rgb` / `#rrggbb` / `#rrggbbaa`). Default `#FFFF00`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub color: Option<String>,
    /// Stroke width in pixels. Default `2`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub thickness: Option<f64>,
    /// Vertex marker radius; `0` hides markers. Default `4`.
    #[serde(rename = "markerSize", skip_serializing_if = "Option::is_none")]
    pub marker_size: Option<f64>,
    /// `solid` | `dashed` | `dotted`. Default `solid`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub style: Option<String>,
    /// When true the shape is closed and `fillColor` is filled. Default `false`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub locked: Option<bool>,
    /// Fill color for a closed shape, or `transparent`. Default `transparent`.
    #[serde(rename = "fillColor", skip_serializing_if = "Option::is_none")]
    pub fill_color: Option<String>,
}

/// A single point in image-pixel space.
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct Point {
    pub x: f64,
    pub y: f64,
}

/// Serialize an inline layout to a temp `*.json` file for `--layout`. The returned handle
/// keeps the file on disk until it is dropped, so hold it until the CLI has run.
pub fn write_temp(layout: &Layout) -> anyhow::Result<tempfile::NamedTempFile> {
    let json = serde_json::to_vec_pretty(layout)?;
    let mut file = tempfile::Builder::new()
        .prefix("stencil-layout-")
        .suffix(".json")
        .tempfile()?;
    file.write_all(&json)?;
    file.flush()?;
    Ok(file)
}
