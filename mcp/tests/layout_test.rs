//! The layout JSON contract (pure).
//!
//! `layout.rs` is one end of a cross-surface agreement: the browser exports this document
//! (`browser/js/core/layout.js` → buildLayoutPayload), the Zig CLI parses it
//! (`cli/src/layout.zig`), and this server writes it to a temp file for `--layout`. Nothing
//! type-checks the three against each other, so a renamed key or a stray `null` would only
//! show up as a silently mis-drawn image. These tests pin the wire shape.

use stencil_mcp::layout::{write_temp, Layout, Line, Point};

fn to_json(layout: &Layout) -> serde_json::Value {
    serde_json::to_value(layout).expect("layout serializes")
}

fn bare_line() -> Line {
    Line {
        points: vec![Point { x: 1.0, y: 2.0 }, Point { x: 3.0, y: 4.0 }],
        color: None,
        thickness: None,
        point_size: None,
        style: None,
        locked: None,
        fill_color: None,
        point_color: None,
    }
}

/// Omitted per-line fields must be ABSENT, not null. The CLI applies its own defaults with
/// `if (lo.get("thickness"))`-style lookups, so a serialized `null` would be found and
/// coerced to 0 rather than falling back to the documented default.
#[test]
fn omitted_line_fields_are_absent_not_null() {
    let json = to_json(&Layout {
        image_width: None,
        image_height: None,
        filter: None,
        lines: vec![bare_line()],
    });

    let obj = json.as_object().expect("layout is an object");
    for key in ["imageWidth", "imageHeight", "imageFilter", "filter"] {
        assert!(
            !obj.contains_key(key),
            "top-level `{key}` should be omitted"
        );
    }
    assert_eq!(obj.len(), 1, "only `lines` should be present: {json}");

    let line = &json["lines"][0];
    let line_obj = line.as_object().expect("line is an object");
    for key in [
        "color",
        "thickness",
        "pointSize",
        "style",
        "locked",
        "fillColor",
    ] {
        assert!(
            !line_obj.contains_key(key),
            "line field `{key}` should be omitted, got {line}"
        );
    }
    assert_eq!(line_obj.len(), 1, "only `points` should be present: {line}");
}

/// Points are always `{x, y}` numbers — the CLI reads them positionally by name.
#[test]
fn points_serialize_as_x_y_numbers() {
    let json = to_json(&Layout {
        image_width: None,
        image_height: None,
        filter: None,
        lines: vec![bare_line()],
    });
    assert_eq!(json["lines"][0]["points"][0]["x"], 1.0);
    assert_eq!(json["lines"][0]["points"][0]["y"], 2.0);
    assert_eq!(json["lines"][0]["points"][1]["x"], 3.0);
    assert_eq!(json["lines"][0]["points"][1]["y"], 4.0);
}

/// The three renamed fields are the ones a Rust-side rename would quietly break: Rust
/// snake_case must reach the wire as the camelCase the browser and CLI use.
#[test]
fn renamed_fields_reach_the_wire_as_camel_case() {
    let json = to_json(&Layout {
        image_width: Some(640.0),
        image_height: Some(480.0),
        filter: Some("bw".into()),
        lines: vec![Line {
            points: vec![Point { x: 0.0, y: 0.0 }],
            color: Some("#ff0000".into()),
            thickness: Some(3.0),
            point_size: Some(6.0),
            style: Some("dashed".into()),
            locked: Some(true),
            fill_color: Some("#00ff00".into()),
            point_color: None,
        }],
    });

    assert_eq!(json["imageWidth"], 640.0);
    assert_eq!(json["imageHeight"], 480.0);
    assert_eq!(json["imageFilter"], "bw");
    assert!(json.get("filter").is_none(), "legacy `filter` key must not be written");
    let line = &json["lines"][0];
    assert_eq!(line["color"], "#ff0000");
    assert_eq!(line["thickness"], 3.0);
    assert_eq!(line["pointSize"], 6.0);
    assert_eq!(line["style"], "dashed");
    assert_eq!(line["locked"], true);
    assert_eq!(line["fillColor"], "#00ff00");
    // Nothing snake_case leaked through.
    for stale in ["image_width", "image_height", "point_size", "fill_color"] {
        assert!(
            json.get(stale).is_none() && line.get(stale).is_none(),
            "snake_case key `{stale}` leaked onto the wire"
        );
    }
}

/// The exact document the CLI's own parser test feeds `cli/src/layout.zig` (it spells the
/// filter with the legacy `filter` key, which both ends still read). If the two ends ever
/// disagree about a key, this stops deserializing what that test asserts.
#[test]
fn parses_the_document_the_cli_parser_test_uses() {
    let doc = r#"{ "imageWidth": 10, "imageHeight": 20, "filter": "bw",
        "lines": [ { "points": [{"x":1,"y":2},{"x":3,"y":4}],
                     "color": "red", "thickness": 3, "locked": true } ] }"#;
    let layout: Layout = serde_json::from_str(doc).expect("the CLI's fixture parses here too");

    assert_eq!(layout.image_width, Some(10.0));
    assert_eq!(layout.image_height, Some(20.0));
    assert_eq!(layout.filter.as_deref(), Some("bw"));
    assert_eq!(layout.lines.len(), 1);

    let line = &layout.lines[0];
    assert_eq!(line.points.len(), 2);
    assert_eq!(line.color.as_deref(), Some("red"));
    assert_eq!(line.thickness, Some(3.0));
    assert_eq!(line.locked, Some(true));
    // Absent in the document → None here, so the CLI supplies its documented defaults
    // (pointSize 4, style solid, fillColor transparent).
    assert_eq!(line.point_size, None);
    assert_eq!(line.style, None);
    assert_eq!(line.fill_color, None);
}

/// Filter key read-both (Phase 6): canonical `imageFilter` and the legacy `filter`
/// spelling both deserialize into the same field. Spelling BOTH in one document is a
/// serde duplicate-field error — mcp's strict parser has no both-present precedence.
#[test]
fn filter_reads_canonical_and_legacy_keys() {
    let canonical: Layout = serde_json::from_str(r#"{"imageFilter":"bw"}"#).expect("parses");
    assert_eq!(canonical.filter.as_deref(), Some("bw"));
    let legacy: Layout = serde_json::from_str(r#"{"filter":"sepia"}"#).expect("parses");
    assert_eq!(legacy.filter.as_deref(), Some("sepia"));
    let both: Result<Layout, _> = serde_json::from_str(r#"{"filter":"a","imageFilter":"b"}"#);
    assert!(both.is_err(), "both spellings at once is a duplicate-field error");
}

/// A layout with no `lines` key is legal (`#[serde(default)]`) and means "draw nothing" —
/// a filter-only layout is a real use of the flag.
#[test]
fn missing_lines_defaults_to_empty() {
    let layout: Layout = serde_json::from_str(r#"{"filter":"sepia"}"#).expect("parses");
    assert!(layout.lines.is_empty());
    assert_eq!(layout.filter.as_deref(), Some("sepia"));
}

/// A line with no points is legal on the wire; the CLI skips it rather than erroring.
#[test]
fn a_line_with_no_points_round_trips() {
    let layout: Layout = serde_json::from_str(r#"{"lines":[{"points":[]}]}"#).expect("parses");
    assert_eq!(layout.lines.len(), 1);
    assert!(layout.lines[0].points.is_empty());
}

/// Unknown keys from a newer browser export must be ignored, never rejected — an older
/// server should still draw the lines it understands.
#[test]
fn unknown_fields_are_ignored() {
    let doc = r#"{"imageWidth":10,"futureKey":{"a":1},
                  "lines":[{"points":[{"x":0,"y":0}],"futureLineKey":true}]}"#;
    let layout: Layout = serde_json::from_str(doc).expect("unknown keys must not be fatal");
    assert_eq!(layout.image_width, Some(10.0));
    assert_eq!(layout.lines.len(), 1);
}

#[test]
fn round_trips_through_json() {
    let layout = Layout {
        image_width: Some(1.5),
        image_height: Some(2.5),
        filter: Some("#3366ff".into()),
        lines: vec![
            bare_line(),
            Line {
                points: vec![Point { x: -1.0, y: 0.0 }],
                color: Some("yellow".into()),
                thickness: Some(0.0),
                point_size: Some(0.0),
                style: Some("dotted".into()),
                locked: Some(false),
                fill_color: Some("transparent".into()),
                point_color: None,
            },
        ],
    };
    let json = serde_json::to_string(&layout).expect("serializes");
    let back: Layout = serde_json::from_str(&json).expect("deserializes");
    assert_eq!(layout, back);
}

/// `write_temp` is what turns an inline layout into something `--layout` can open. The
/// handle must keep the file alive (the pipeline holds it across the CLI spawn), the
/// suffix must be `.json`, and the bytes must parse back to the same layout.
#[test]
fn write_temp_produces_a_readable_json_file() {
    let layout = Layout {
        image_width: Some(320.0),
        image_height: None,
        filter: None,
        lines: vec![bare_line()],
    };
    let file = write_temp(&layout).expect("writes a temp layout");
    let path = file.path().to_path_buf();

    assert!(path.is_file(), "the handle must keep the file on disk");
    assert_eq!(
        path.extension().and_then(|e| e.to_str()),
        Some("json"),
        "the CLI selects its parser by extension: {}",
        path.display()
    );
    assert!(path
        .file_name()
        .and_then(|n| n.to_str())
        .is_some_and(|n| n.starts_with("stencil-layout-")));

    let text = std::fs::read_to_string(&path).expect("readable");
    let back: Layout = serde_json::from_str(&text).expect("the file holds valid layout JSON");
    assert_eq!(back, layout);

    // Dropping the handle removes the file — the pipeline relies on this for cleanup.
    drop(file);
    assert!(!path.exists(), "the temp layout should be removed on drop");
}
