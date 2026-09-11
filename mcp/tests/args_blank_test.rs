//! `blank` parameters → argv: dims, page formats and colours, and what each rejects.
mod common;
use common::args::params;

use serde_json::json;
use stencil_mcp::args::build_argv;

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
        let err = build_argv(&p, None).unwrap_err().to_string();
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
        let err = build_argv(&p, None).unwrap_err().to_string();
        assert!(err.contains("not a recognized color"), "{bad:?} got: {err}");
    }
}

#[test]
fn blank_page_and_dims_are_rejected() {
    let p = params(json!({
        "blank": { "page": "B5", "width": 800, "height": 600 }, "output": "out.png"
    }));
    let err = build_argv(&p, None).unwrap_err().to_string();
    assert!(err.contains("mutually exclusive"), "got: {err}");
}

fn blank_half_dimensions_are_rejected() {
    let p = params(json!({ "blank": { "width": 800 }, "output": "out.png" }));
    let err = build_argv(&p, None).unwrap_err().to_string();
    assert!(err.contains("together"), "got: {err}");
}
