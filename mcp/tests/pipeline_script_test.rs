//! A whole `stencil_script` run without a CLI binary: the confinement rewrite, the spawn
//! directory, and the per-`@save` outcome parsing, against the recording runner.

use serde_json::json;
use stencil_mcp::args::ScriptParams;
use stencil_mcp::pipeline::run;

mod common;
use common::cli::FakeCli;

fn params(value: serde_json::Value) -> ScriptParams {
    serde_json::from_value(value).expect("script params should deserialize")
}

/// A temp directory that exists, so the run's `create_dir_all` has something to confine to.
fn root() -> tempfile::TempDir {
    tempfile::Builder::new().prefix("stencil-script-run-").tempdir().expect("a temp root")
}

#[tokio::test]
async fn every_save_becomes_one_reported_file() {
    let dir = root();
    let cli = FakeCli::ok("wrote a-stencil.png (800x600)\nwrote b-stencil.png (40x20)\n");
    let p = params(json!({ "script_path": "tour.stc", "output_dir": dir.path() }));

    let result = run::script(&cli, &p, "tour.stc").await.expect("the run succeeds");

    assert_eq!(result.files.len(), 2);
    assert_eq!(result.files[0].path, dir.path().join("a-stencil.png").to_string_lossy());
    assert_eq!((result.files[1].width, result.files[1].height), (40, 20));
}

/// mcp forwards model-chosen paths, so a script run is ALWAYS confined: it spawns inside the
/// sandbox root and carries `--confine-output`, which is what refuses a climbing `@save`.
#[tokio::test]
async fn the_run_is_confined_to_its_output_directory() {
    let dir = root();
    let cli = FakeCli::ok("wrote out.png (2x2)\n");
    let p = params(json!({ "script_path": "tour.stc", "output_dir": dir.path() }));

    run::script(&cli, &p, "tour.stc").await.expect("the run succeeds");

    let call = cli.call();
    assert!(call.argv.contains(&"--confine-output".to_string()), "got: {:?}", call.argv);
    assert_eq!(call.dir.as_deref(), Some(dir.path()));
}

/// An unconfined run would lose its script (and its input) to the working-directory change,
/// so both ride absolute — which is why `--script` is one of confine's path flags.
#[tokio::test]
async fn the_script_and_input_paths_are_made_absolute() {
    let dir = root();
    let cli = FakeCli::ok("wrote out.png (2x2)\n");
    let p = params(json!({ "script_path": "tour.stc", "input": "a.png", "output_dir": dir.path() }));

    run::script(&cli, &p, "scripts/tour.stc").await.expect("the run succeeds");

    let argv = cli.argv();
    let script = &argv[argv.iter().position(|t| t == "--script").expect("the flag") + 1];
    let input = &argv[argv.iter().position(|t| t == "-i").expect("the flag") + 1];
    assert!(script.starts_with('/') && script.ends_with("scripts/tour.stc"), "got: {script}");
    assert!(input.starts_with('/') && input.ends_with("a.png"), "got: {input}");
}

/// The output directory is the caller's to name, so the run creates it rather than failing.
#[tokio::test]
async fn a_missing_output_directory_is_created() {
    let dir = root();
    let nested = dir.path().join("shots/today");
    let cli = FakeCli::ok("wrote out.png (2x2)\n");
    let p = params(json!({ "script_path": "tour.stc", "output_dir": nested }));

    run::script(&cli, &p, "tour.stc").await.expect("the run succeeds");
    assert!(nested.is_dir());
}

/// A script that saved nothing is a successful run with a note, not a failure.
#[tokio::test]
async fn the_clis_note_lines_ride_back_with_the_result() {
    let dir = root();
    let cli = FakeCli::ok("note: shots/: no files matched\nnote: the script saved nothing\n");
    let p = params(json!({ "script_text": "@source shots/:", "output_dir": dir.path() }));

    let result = run::script(&cli, &p, "/tmp/inline.stc").await.expect("the run succeeds");

    assert!(result.files.is_empty());
    assert_eq!(result.notes, ["shots/: no files matched", "the script saved nothing"]);
}

/// A script with a diagnostic error runs nothing; the caller sees the CLI's own error lines.
#[tokio::test]
async fn a_script_with_errors_fails_with_the_clis_diagnostics() {
    let dir = root();
    let cli = FakeCli::failing("error: tour.stc:3:1: unknown directive '@cropp' [E_UNKNOWN_DIRECTIVE]\n");
    let p = params(json!({ "script_path": "tour.stc", "output_dir": dir.path() }));

    let error = run::script(&cli, &p, "tour.stc").await.expect_err("the run fails");
    assert_eq!(
        error.to_string(),
        "error: tour.stc:3:1: unknown directive '@cropp' [E_UNKNOWN_DIRECTIVE]"
    );
}

/// The parameter guards run before the spawn, so a bad call never reaches the binary.
#[tokio::test]
async fn a_refused_parameter_never_spawns_the_cli() {
    let cli = FakeCli::ok("");
    let error = run::script(&cli, &params(json!({})), "/tmp/s.stc")
        .await
        .expect_err("no script was given");

    assert!(error.to_string().contains("no script"), "got: {error}");
    assert!(cli.never_ran());
}
