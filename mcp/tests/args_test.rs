//! Parameter → argv mapping and validation guards (pure; no CLI binary needed).

use serde_json::json;
use stencil_mcp::args::{build_argv, EditParams};
use stencil_mcp::config::Surface;

fn params(value: serde_json::Value) -> EditParams {
    serde_json::from_value(value).expect("params should deserialize")
}

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
fn blank_with_dims_color_and_album() {
    let p = params(json!({
        "blank": { "width": 800, "height": 600, "color": "red" },
        "album": true,
        "output": "out"
    }));
    assert_eq!(
        build_argv(&p, None).unwrap(),
        ["--blank", "800", "600", "red", "--album", "out"]
    );
}

#[test]
fn blank_default_size() {
    let p = params(json!({ "blank": { "color": "#102030" }, "output": "page.png" }));
    assert_eq!(
        build_argv(&p, None).unwrap(),
        ["--blank", "#102030", "page.png"]
    );
}

#[test]
fn blank_page_format_and_color() {
    // The page token rides argv verbatim; the CLI normalizes the case.
    let p = params(json!({ "blank": { "page": "b5", "color": "pink" }, "output": "page.png" }));
    assert_eq!(
        build_argv(&p, None).unwrap(),
        ["--blank", "b5", "pink", "page.png"]
    );
}

#[test]
fn blank_unknown_page_name_is_rejected() {
    // The CLI silently skips an unrecognized --blank token (it would become a positional
    // output and the blank would come out A4), so the server must reject it up front.
    for bad in ["Letter", "legal", "A11", "A01", "D4", "custom", " A4 ", ""] {
        let p = params(json!({ "blank": { "page": bad }, "output": "page.png" }));
        let err = build_argv(&p, None).unwrap_err();
        assert!(err.contains("not a known page format"), "{bad:?} got: {err}");
    }
}

#[test]
fn blank_page_names_accepted_case_insensitively() {
    for good in ["A0", "a4", "b10", "C7", "c10"] {
        let p = params(json!({ "blank": { "page": good }, "output": "page.png" }));
        assert_eq!(
            build_argv(&p, None).unwrap(),
            ["--blank", good, "page.png"],
            "page name {good:?} should be accepted"
        );
    }
}

#[test]
fn blank_colors_accepted() {
    // Mirrors the core's parseColor grammar: named / transparent / #hex (3/4/6/8 digits).
    for good in ["red", "REBECCAPURPLE", "transparent", "#abc", "#AbCd", "#102030", "#102030ff"] {
        let p = params(json!({ "blank": { "color": good }, "output": "page.png" }));
        assert_eq!(
            build_argv(&p, None).unwrap(),
            ["--blank", good, "page.png"],
            "color {good:?} should be accepted"
        );
    }
}

#[test]
fn blank_unknown_color_is_rejected() {
    // The CLI leaves an unparseable --blank colour unconsumed (it would fall to the
    // positional output slot and the blank would come out white), so reject it up front.
    for bad in ["pinkk", "notacolour", "#12", "#12345", "#gggggg", "rgb(1,2,3)", ""] {
        let p = params(json!({ "blank": { "color": bad }, "output": "page.png" }));
        let err = build_argv(&p, None).unwrap_err();
        assert!(err.contains("not a recognized color"), "{bad:?} got: {err}");
    }
}

#[test]
fn blank_page_and_dims_are_rejected() {
    let p = params(json!({
        "blank": { "page": "B5", "width": 800, "height": 600 }, "output": "out.png"
    }));
    let err = build_argv(&p, None).unwrap_err();
    assert!(err.contains("mutually exclusive"), "got: {err}");
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
    let err = build_argv(&p, None).unwrap_err();
    assert!(err.contains("mutually exclusive"), "got: {err}");
}

#[test]
fn missing_source_is_rejected() {
    let p = params(json!({ "output": "out.png" }));
    let err = build_argv(&p, None).unwrap_err();
    assert!(err.contains("no source"), "got: {err}");
}

#[test]
fn blank_half_dimensions_are_rejected() {
    let p = params(json!({ "blank": { "width": 800 }, "output": "out.png" }));
    let err = build_argv(&p, None).unwrap_err();
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

// ── collaboration server ──────────────────────────────────────────────────────

#[test]
fn server_fetch_and_remote_update() {
    let p = params(json!({
        "server": "http://h:8090", "input": "Shared", "filter": "sepia",
        "remote_update": true, "output": "out.png"
    }));
    assert_eq!(
        build_argv(&p, None).unwrap(),
        [
            "--server",
            "http://h:8090",
            "-i",
            "Shared",
            "--filter",
            "sepia",
            "--remote-update",
            "out.png"
        ]
    );
}

#[test]
fn remote_create_with_name() {
    let p = params(json!({
        "input": "photo.png", "rotate": 1,
        "remote": "http://h:8090", "remote_name": "Shared", "output": "out.png"
    }));
    assert_eq!(
        build_argv(&p, None).unwrap(),
        [
            "-i",
            "photo.png",
            "-r",
            "1",
            "--remote",
            "http://h:8090",
            "--remote-name",
            "Shared",
            "out.png"
        ]
    );
}

#[test]
fn fetch_from_one_server_publish_to_another() {
    // One call can fetch a project from one server and create it on a different one.
    let p = params(json!({
        "server": "http://a:8090", "input": "Plans",
        "remote": "http://b:8090", "remote_name": "Plans copy", "output": "out.png"
    }));
    let argv = build_argv(&p, None).unwrap();
    assert_eq!(argv[..2], ["--server", "http://a:8090"]);
    let r = argv.iter().position(|a| a == "--remote").unwrap();
    assert_eq!(argv[r + 1], "http://b:8090");
}

#[test]
fn server_without_input_is_rejected() {
    let p = params(json!({ "server": "http://h:8090", "blank": {}, "output": "out.png" }));
    let err = build_argv(&p, None).unwrap_err();
    assert!(err.contains("server"), "got: {err}");
}

#[test]
fn server_with_blank_is_rejected() {
    // blank carries a source, so `input` is present — but `server` still can't take a blank.
    let p = params(json!({
        "server": "http://h:8090", "input": "Shared", "blank": {}, "output": "out.png"
    }));
    let err = build_argv(&p, None).unwrap_err();
    assert!(err.contains("blank"), "got: {err}");
}

#[test]
fn remote_update_without_server_is_rejected() {
    let p = params(json!({ "input": "a.png", "remote_update": true, "output": "out.png" }));
    let err = build_argv(&p, None).unwrap_err();
    assert!(err.contains("remote_update"), "got: {err}");
}

#[test]
fn remote_name_without_remote_is_rejected() {
    let p = params(json!({ "input": "a.png", "remote_name": "X", "output": "out.png" }));
    let err = build_argv(&p, None).unwrap_err();
    assert!(err.contains("remote_name"), "got: {err}");
}
