//! Collaboration-server parameters → argv: fetch, remote-update, publish, and what they reject.
mod common;
use common::args::params;

use serde_json::json;
use stencil_mcp::args::build_argv;

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
    let err = build_argv(&p, None).unwrap_err().to_string();
    assert!(err.contains("server"), "got: {err}");
}

#[test]
fn server_with_blank_is_rejected() {
    // blank carries a source, so `input` is present — but `server` still can't take a blank.
    let p = params(json!({
        "server": "http://h:8090", "input": "Shared", "blank": {}, "output": "out.png"
    }));
    let err = build_argv(&p, None).unwrap_err().to_string();
    assert!(err.contains("blank"), "got: {err}");
}

#[test]
fn remote_update_without_server_is_rejected() {
    let p = params(json!({ "input": "a.png", "remote_update": true, "output": "out.png" }));
    let err = build_argv(&p, None).unwrap_err().to_string();
    assert!(err.contains("remote_update"), "got: {err}");
}

#[test]
fn remote_name_without_remote_is_rejected() {
    let p = params(json!({ "input": "a.png", "remote_name": "X", "output": "out.png" }));
    let err = build_argv(&p, None).unwrap_err().to_string();
    assert!(err.contains("remote_name"), "got: {err}");
}
