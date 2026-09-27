//! The script modes that run nothing — check, plan, emit — against the recording runner:
//! the argv each tool builds, what comes back on stdout, and emit's confinement.

use serde_json::json;
use stencil_mcp::args::{
    build_check_argv, build_emit_argv, build_plan_argv, ScriptCheckParams, ScriptEmitParams,
    ScriptPlanParams,
};
use stencil_mcp::pipeline::script;

mod common;
use common::cli::FakeCli;

fn check_params() -> ScriptCheckParams {
    serde_json::from_value(json!({ "script_path": "tour.stc" })).unwrap()
}

#[tokio::test]
async fn a_check_reports_its_diagnostics_and_whether_the_script_is_valid() {
    let dir = tempfile::tempdir().unwrap();
    let stdout = "tour.stc:2:3: error: unknown directive '@crp' [E_UNKNOWN_DIRECTIVE]\n";
    let cli = FakeCli::printing(false, stdout, "");
    let argv = build_check_argv(&check_params(), "/abs/tour.stc").unwrap();
    let check = script::check(&cli, &argv, dir.path()).await.expect("a check ran");

    assert!(!check.valid);
    assert_eq!(check.diagnostics[0].code, "E_UNKNOWN_DIRECTIVE");
    assert_eq!(cli.argv(), ["--script-check", "/abs/tour.stc"]);
}

/// A failure with no error diagnostic is not a verdict on the script: it is the CLI's error.
#[tokio::test]
async fn an_unreadable_script_is_the_clis_own_error() {
    let dir = tempfile::tempdir().unwrap();
    let cli = FakeCli::printing(false, "", "error: could not read tour.stc (FileNotFound)\n");
    let argv = build_check_argv(&check_params(), "/abs/tour.stc").unwrap();
    let error = script::check(&cli, &argv, dir.path()).await.unwrap_err();
    assert_eq!(error, "error: could not read tour.stc (FileNotFound)");
}

#[tokio::test]
async fn a_plan_is_the_envelope_the_cli_printed() {
    let dir = tempfile::tempdir().unwrap();
    let envelope = r#"{"version":1,"script":"t.stc","diagnostics":[],"blocks":[{"index":0}]}"#;
    let cli = FakeCli::printing(true, envelope, "");
    let params: ScriptPlanParams =
        serde_json::from_value(json!({ "script_text": "@crop 10%", "input": "a.png" })).unwrap();
    let argv = build_plan_argv(&params, "/tmp/inline.stc").unwrap();
    let plan = script::plan(&cli, &argv, dir.path()).await.expect("a plan");

    assert_eq!(plan["blocks"][0]["index"], 0);
    let argv = cli.argv();
    assert_eq!(argv[..3], ["--script-plan", "/tmp/inline.stc", "-i"]);
    let input = &argv[3];
    assert!(input.starts_with('/') && input.ends_with("a.png"), "input made absolute: {input}");
}

/// Emit writes under `--confine-output` with its output relative to the root it runs in, leaves
/// the clobber check to the CLI's `--no-clobber` unless asked to overwrite, and reports the
/// CLI's own `(python)` / `(javascript)` line.
#[tokio::test]
async fn an_emit_is_confined_and_reports_its_target() {
    let root = tempfile::tempdir().unwrap();
    let out = root.path().join("tour.pystc");
    let params: ScriptEmitParams =
        serde_json::from_value(json!({ "script_path": "tour.stc", "output": out })).unwrap();
    let argv = build_emit_argv(&params, "/abs/tour.stc").unwrap();

    let cli = FakeCli::ok("wrote tour.pystc (python)\n");
    let emitted = script::emit(&cli, &argv, root.path()).await.expect("emitted");
    assert_eq!(emitted.path, out.to_string_lossy());
    assert_eq!(emitted.target, "python");
    let call = cli.call();
    let expected =
        ["--script", "/abs/tour.stc", "--no-clobber", "--confine-output", "--script-emit", "tour.pystc"];
    assert_eq!(call.argv, expected);
    assert_eq!(call.dir.as_deref(), Some(root.path()));

    let cli = FakeCli::failing("error: --no-clobber: 'tour.pystc' already exists\n");
    let error = script::emit(&cli, &argv, root.path()).await.unwrap_err();
    assert!(error.contains("already exists") && error.contains("overwrite=true"), "{error}");

    let replace: ScriptEmitParams = serde_json::from_value(
        json!({ "script_path": "tour.stc", "output": out, "overwrite": true }),
    )
    .unwrap();
    let argv = build_emit_argv(&replace, "/abs/tour.stc").unwrap();
    assert!(!argv.iter().any(|a| a == "--no-clobber"), "{argv:?}");

    let elsewhere = tempfile::tempdir().unwrap();
    let error = script::emit(&cli, &argv, elsewhere.path()).await.unwrap_err();
    assert!(error.contains("refusing to write outside"), "{error}");
}
