//! The layout JSON the CLI's `--layout` flag draws onto an image.
//!
//! These types mirror the schema the browser exports and the CLI parses
//! (`cli/src/media/layout.zig` ← `core/raster`). Coordinates are image pixels; a line is a
//! polyline through its `points`, closed and filled by repeating the first point.

use std::io::Write;

use schemars::JsonSchema;
use serde::{Deserialize, Serialize};

/// A full layout: optional source dimensions/filter/page format plus the lines to draw.
/// Field order IS the emitted key order; it follows `browser/js/config/layoutFields.json`.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize, JsonSchema)]
pub struct Layout {
    /// Source image width in pixels (advisory; the CLI draws against the live image).
    #[serde(rename = "imageWidth", skip_serializing_if = "Option::is_none")]
    pub image_width: Option<f64>,
    /// Source image height in pixels (advisory).
    #[serde(rename = "imageHeight", skip_serializing_if = "Option::is_none")]
    pub image_height: Option<f64>,
    /// The polylines to burn into the image.
    #[serde(default)]
    pub lines: Vec<Line>,
    /// Filter baked into the layout (`bw`/`sepia`/`invert`/`contour`/a color); a top-level
    /// `filter` argument overrides it. Canonical wire key is `imageFilter`.
    #[serde(rename = "imageFilter", alias = "filter", skip_serializing_if = "Option::is_none")]
    pub filter: Option<String>,
    /// Page format the coordinates were authored against: a named ISO size (`A0`..`C10`) or
    /// `custom`. The CLI reports it in the `wrote` line; it never resizes the image.
    #[serde(rename = "pageSize", skip_serializing_if = "Option::is_none")]
    pub page_size: Option<String>,
    /// Page width in cm, read only when `pageSize` is `custom`.
    #[serde(rename = "customPageWidth", skip_serializing_if = "Option::is_none")]
    pub custom_page_width: Option<f64>,
    /// Page height in cm, read only when `pageSize` is `custom`.
    #[serde(rename = "customPageHeight", skip_serializing_if = "Option::is_none")]
    pub custom_page_height: Option<f64>,
}

/// One polyline / closed shape.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, JsonSchema)]
pub struct Line {
    /// Vertices in image-pixel space. A line with no points is skipped by the CLI.
    pub points: Vec<Point>,
    /// Stroke color (CSS name / `#rgb` / `#rrggbb` / `#rrggbbaa`). Default `#FFFF00`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub color: Option<String>,
    /// Stroke width in pixels. Default `2`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub thickness: Option<f64>,
    /// Vertex point radius; `0` hides points. Default `4`.
    #[serde(rename = "pointSize", skip_serializing_if = "Option::is_none")]
    pub point_size: Option<f64>,
    /// `solid` | `dashed` | `dotted`. Default `solid`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub style: Option<String>,
    /// When true the shape is closed and `fillColor` is filled. Default `false`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub locked: Option<bool>,
    /// Fill color for a closed shape, or `transparent`. Default `transparent`.
    #[serde(rename = "fillColor", skip_serializing_if = "Option::is_none")]
    pub fill_color: Option<String>,
    /// Vertex point color, independent of `color`. Omitted / empty means the points inherit
    /// `color` (core `Line::pointColor`).
    #[serde(rename = "pointColor", skip_serializing_if = "Option::is_none")]
    pub point_color: Option<String>,
}

/// A single point in image-pixel space.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, JsonSchema)]
pub struct Point {
    pub x: f64,
    pub y: f64,
}

/// Serialize an inline layout to a temp `*.json` file for `--layout`. The returned handle
/// keeps the file on disk until it is dropped, so hold it until the CLI has run.
pub fn write_temp(layout: &Layout) -> Result<tempfile::NamedTempFile, Box<dyn std::error::Error>> {
    let json = serde_json::to_vec_pretty(layout)?;
    let mut file = tempfile::Builder::new()
        .prefix("stencil-layout-")
        .suffix(".json")
        .tempfile()?;
    file.write_all(&json)?;
    file.flush()?;
    Ok(file)
}
