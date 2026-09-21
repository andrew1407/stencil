//! What mcp WRITES as layout JSON (pure).
//!
//! `layout.rs` is one end of a cross-surface agreement: the browser exports this document
//! (`browser/js/core/layout.js`), the Zig CLI parses it (`cli/src/media/layout.zig`), and this
//! server writes it for `--layout`. Nothing type-checks the three against each other.

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

/// Omitted per-line fields must be ABSENT, not null: the CLI's `if (lo.get("thickness"))`
/// lookups would find a `null` and coerce it to 0 instead of its documented default.
#[test]
fn omitted_line_fields_are_absent_not_null() {
    let json = to_json(&Layout { lines: vec![bare_line()], ..Layout::default() });

    let obj = json.as_object().expect("layout is an object");
    for key in [
        "imageWidth",
        "imageHeight",
        "imageFilter",
        "filter",
        "pageSize",
        "customPageWidth",
        "customPageHeight",
    ] {
        assert!(
            !obj.contains_key(key),
            "top-level `{key}` should be omitted"
        );
    }
    assert_eq!(obj.len(), 1, "only `lines` should be present: {json}");

    let line = &json["lines"][0];
    let line_obj = line.as_object().expect("line is an object");
    for key in ["color", "thickness", "pointSize", "style", "locked", "fillColor"] {
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
    let json = to_json(&Layout { lines: vec![bare_line()], ..Layout::default() });
    assert_eq!(json["lines"][0]["points"][0]["x"], 1.0);
    assert_eq!(json["lines"][0]["points"][0]["y"], 2.0);
    assert_eq!(json["lines"][0]["points"][1]["x"], 3.0);
    assert_eq!(json["lines"][0]["points"][1]["y"], 4.0);
}

/// The renamed fields are the ones a Rust-side rename would quietly break: Rust
/// snake_case must reach the wire as the camelCase the browser and CLI use.
#[test]
fn renamed_fields_reach_the_wire_as_camel_case() {
    let json = to_json(&Layout {
        image_width: Some(640.0),
        image_height: Some(480.0),
        filter: Some("bw".into()),
        page_size: Some("custom".into()),
        custom_page_width: Some(10.5),
        custom_page_height: Some(14.8),
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
    assert_eq!(json["pageSize"], "custom");
    assert_eq!(json["customPageWidth"], 10.5);
    assert_eq!(json["customPageHeight"], 14.8);
    assert!(json.get("filter").is_none(), "legacy `filter` key must not be written");
    let line = &json["lines"][0];
    assert_eq!(line["color"], "#ff0000");
    assert_eq!(line["thickness"], 3.0);
    assert_eq!(line["pointSize"], 6.0);
    assert_eq!(line["style"], "dashed");
    assert_eq!(line["locked"], true);
    assert_eq!(line["fillColor"], "#00ff00");
    // Nothing snake_case leaked through.
    for stale in ["image_width", "image_height", "page_size", "custom_page_width", "point_size", "fill_color"] {
        assert!(
            json.get(stale).is_none() && line.get(stale).is_none(),
            "snake_case key `{stale}` leaked onto the wire"
        );
    }
}

#[test]
fn round_trips_through_json() {
    let layout = Layout {
        image_width: Some(1.5),
        image_height: Some(2.5),
        filter: Some("#3366ff".into()),
        page_size: Some("A4".into()),
        custom_page_width: Some(0.0),
        custom_page_height: None,
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

/// `write_temp` turns an inline layout into something `--layout` can open: the handle keeps
/// the file alive across the CLI spawn, the suffix is `.json`, the bytes parse back.
#[test]
fn write_temp_produces_a_readable_json_file() {
    let layout = Layout {
        image_width: Some(320.0),
        lines: vec![bare_line()],
        ..Layout::default()
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
