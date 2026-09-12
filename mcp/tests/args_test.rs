//! Parameter → argv mapping: the CLI's fixed pipeline order, and the `surface` override.
mod common;
use common::args::params;

use serde_json::json;
use stencil_mcp::args::build_argv;
use stencil_mcp::config::Surface;

#[test]
fn minimal_input_output() {
    let p = params(json!({ "input": "a.png", "output": "out.png" }));
    assert_eq!(build_argv(&p, None).unwrap(), ["-i", "a.png", "out.png"]);
}

#[test]
fn full_pipeline_order_and_flags() {
    let p = params(json!({
        "input": "photo.jpg",
        "crop": "x1=10% x2=90% y1=10% y2=90%",
        "rotate": 1,
        "filter": "sepia",
        "output": "out.png"
    }));
    assert_eq!(
        build_argv(&p, Some("/tmp/layout.json")).unwrap(),
        [
            "-i",
            "photo.jpg",
            "-c",
            "x1=10% x2=90% y1=10% y2=90%",
            "-r",
            "1",
            "-l",
            "/tmp/layout.json",
            "--filter",
            "sepia",
            "out.png"
        ]
    );
}

#[test]
fn layout_frame_flag_rides_with_the_layout_only() {
    // The field is #[serde(skip)] — tool callers can't set it, so it deserializes unset…
    let mut p = params(json!({ "input": "a.png", "output": "out.png" }));
    assert_eq!(p.layout_frame, None);
    // …and the op-plan executor sets it internally; it is emitted right after -l.
    p.layout_frame = Some("source".to_string());
    assert_eq!(
        build_argv(&p, Some("/tmp/layout.json")).unwrap(),
        ["-i", "a.png", "-l", "/tmp/layout.json", "--layout-frame", "source", "out.png"]
    );
    // Without a layout path the flag is meaningless and omitted.
    assert_eq!(build_argv(&p, None).unwrap(), ["-i", "a.png", "out.png"]);
}

#[test]
fn crop_object_renders_to_spec() {
    let p = params(json!({
        "input": "a.png",
        "crop": { "x1": "10%", "x2": "-10%", "y2": "200px" },
        "output": "out.png"
    }));
    let argv = build_argv(&p, None).unwrap();
    let i = argv.iter().position(|a| a == "-c").unwrap();
    assert_eq!(argv[i + 1], "x1=10% x2=-10% y2=200px");
}

#[test]
fn negative_rotate_is_passed_through() {
    let p = params(json!({ "input": "a.png", "rotate": -1, "output": "out.png" }));
    let argv = build_argv(&p, None).unwrap();
    let i = argv.iter().position(|a| a == "-r").unwrap();
    assert_eq!(argv[i + 1], "-1");
}

#[test]
fn frame_flag_for_video() {
    let p = params(json!({ "input": "clip.mp4", "frame": 24, "output": "f.png" }));
    assert_eq!(
        build_argv(&p, None).unwrap(),
        ["-i", "clip.mp4", "-f", "24", "f.png"]
    );
}

#[test]
fn input_and_blank_are_mutually_exclusive() {
    let p = params(json!({ "input": "a.png", "blank": {}, "output": "out.png" }));
    let err = build_argv(&p, None).unwrap_err().to_string();
    assert!(err.contains("mutually exclusive"), "got: {err}");
}

#[test]
fn missing_source_is_rejected() {
    let p = params(json!({ "output": "out.png" }));
    let err = build_argv(&p, None).unwrap_err().to_string();
    assert!(err.contains("no source"), "got: {err}");
}

#[test]
fn blank_half_dimensions_are_rejected() {
    let p = params(json!({ "blank": { "width": 800 }, "output": "out.png" }));
    let err = build_argv(&p, None).unwrap_err().to_string();
    assert!(err.contains("together"), "got: {err}");
}

#[test]
fn surface_defaults_when_omitted() {
    let p = params(json!({ "input": "a.png", "output": "out.png" }));
    let default = [Surface::Cli];
    assert_eq!(p.resolve_surfaces(&default).unwrap(), vec![Surface::Cli]);
}

#[test]
fn surface_single_string_override() {
    let p = params(json!({ "input": "a.png", "output": "out.png", "surface": "browser" }));
    assert_eq!(
        p.resolve_surfaces(&[Surface::Cli]).unwrap(),
        vec![Surface::Cli, Surface::Browser]
    );
}

#[test]
fn surface_list_override() {
    let p = params(json!({
        "input": "a.png", "output": "out.png", "surface": ["desktop", "browser"]
    }));
    assert_eq!(
        p.resolve_surfaces(&[Surface::Cli]).unwrap(),
        vec![Surface::Cli, Surface::Desktop, Surface::Browser]
    );
}

#[test]
fn surface_bad_token_errors() {
    let p = params(json!({ "input": "a.png", "output": "out.png", "surface": "nope" }));
    assert!(p.resolve_surfaces(&[Surface::Cli]).is_err());
}
