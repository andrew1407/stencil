//! `stencil_script` parameters → argv, and the guards that run before any file is written.

use serde_json::json;
use stencil_mcp::args::{build_script_argv, ScriptParams, MAX_SCRIPT_BYTES};

fn script_params(value: serde_json::Value) -> ScriptParams {
    serde_json::from_value(value).expect("script params should deserialize")
}

fn refuse(value: serde_json::Value) -> String {
    build_script_argv(&script_params(value), "/tmp/s.stc")
        .expect_err("the guard refuses")
        .to_string()
}

#[test]
fn inline_text_rides_as_the_resolved_script_file() {
    let p = script_params(json!({ "script_text": "@filter bw\n@save out.png\n" }));
    assert_eq!(
        build_script_argv(&p, "/tmp/stencil-script-1.stc").unwrap(),
        ["--script", "/tmp/stencil-script-1.stc"]
    );
}

#[test]
fn an_input_rides_as_the_sourceless_scripts_working_image() {
    let p = script_params(json!({ "script_path": "tour.stc", "input": "a.png" }));
    assert_eq!(
        build_script_argv(&p, "tour.stc").unwrap(),
        ["--script", "tour.stc", "-i", "a.png"]
    );
}

/// There is no positional output: a script's writes are its own `@save` targets.
#[test]
fn the_argv_carries_no_positional_output() {
    let p = script_params(json!({ "script_text": "@save out.png", "output_dir": "/tmp/run" }));
    let argv = build_script_argv(&p, "/tmp/s.stc").unwrap();
    assert!(!argv.iter().any(|token| token.contains("/tmp/run")), "got: {argv:?}");
}

#[test]
fn exactly_one_script_source_is_required() {
    assert!(refuse(json!({})).contains("no script"));
    let both = refuse(json!({ "script_text": "@save", "script_path": "a.stc" }));
    assert!(both.contains("mutually exclusive"), "got: {both}");
    assert!(refuse(json!({ "script_text": "   \n" })).contains("no script"));
}

#[test]
fn an_oversized_inline_script_is_refused_by_the_byte_cap() {
    let text = "#".repeat(MAX_SCRIPT_BYTES + 1);
    let message = refuse(json!({ "script_text": text }));
    assert!(message.contains(&(MAX_SCRIPT_BYTES + 1).to_string()), "got: {message}");
    assert!(message.contains("script_path"), "got: {message}");

    // The cap itself still passes — it is a limit, not a threshold.
    let p = script_params(json!({ "script_text": "#".repeat(MAX_SCRIPT_BYTES) }));
    assert!(build_script_argv(&p, "/tmp/s.stc").is_ok());
}

#[test]
fn a_script_path_must_name_a_stc_file() {
    assert!(refuse(json!({ "script_path": "notes.txt" })).contains("not a .stc file"));
    assert!(refuse(json!({ "script_path": "tour" })).contains("not a .stc file"));
    // The extension is matched case-insensitively; only the language's paths are not.
    let p = script_params(json!({ "script_path": "Tour.STC" }));
    assert!(build_script_argv(&p, "Tour.STC").is_ok());
}

/// The CLI has no `--` end-of-options terminator, so a dash-leading value would parse as a
/// flag — and `-i` would even swallow the next token.
#[test]
fn no_value_may_lead_with_a_dash_or_be_empty() {
    for (field, value) in [("input", "--filter"), ("output_dir", "-o")] {
        let mut call = json!({ "script_text": "@save out.png" });
        call[field] = json!(value);
        let message = refuse(call);
        assert!(message.contains(field), "got: {message}");
        assert!(message.contains("parsed as a CLI flag"), "got: {message}");
    }
    let dashed = refuse(json!({ "script_path": "-rf.stc" }));
    assert!(dashed.contains("script_path") && dashed.contains("parsed as a CLI flag"));
    assert!(refuse(json!({ "script_path": "" })).contains("must not be empty"));
}

#[test]
fn the_sandbox_root_defaults_to_the_working_directory() {
    assert_eq!(script_params(json!({})).root(), ".");
    assert_eq!(script_params(json!({ "output_dir": "  " })).root(), ".");
    assert_eq!(script_params(json!({ "output_dir": "/tmp/run" })).root(), "/tmp/run");
}
