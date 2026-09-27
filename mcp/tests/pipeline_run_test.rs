//! The whole edit / project run, without a CLI binary. `pipeline::run` is generic over
//! `CliRunner`, so everything AROUND the spawn — the confinement, `--no-clobber`, the server
//! tokens, the inline-layout temp file, the argv, the outcome parsing — runs here against the
//! recording runner in `common::cli`. Scrape: `pipeline_scrape_test.rs`; probe:
//! `pipeline_probe_test.rs`.

use std::path::Path;

use stencil_mcp::args::EditParams;
use stencil_mcp::pipeline::run;

mod common;
use common::cli::FakeCli;

/// Params confined to `root`, their output a file inside it.
fn confined(root: &Path, mut value: serde_json::Value) -> EditParams {
    let output = value["output"].as_str().unwrap_or("out.png").to_string();
    value["output"] = root.join(output).to_string_lossy().into();
    let mut params: EditParams = serde_json::from_value(value).expect("params deserialize");
    params.confine_root = Some(root.to_string_lossy().into_owned());
    params
}

#[tokio::test]
async fn a_successful_run_becomes_the_wrote_line_and_its_dimensions() {
    let dir = tempfile::tempdir().unwrap();
    let cli = FakeCli::ok("wrote out.png (800x600)\n");
    let p = confined(dir.path(), serde_json::json!({ "input": "a.png", "output": "out.png" }));
    let result = run::edit(&cli, &p).await.expect("the run succeeds");

    assert_eq!(result.path, dir.path().join("out.png").to_string_lossy());
    assert_eq!((result.width, result.height), (Some(800), Some(600)));
    let call = cli.call();
    assert_eq!(call.argv[call.argv.len() - 2..], ["--confine-output", "out.png"]);
}

/// Every run is confined: an edit that names no root never reaches the CLI.
#[tokio::test]
async fn an_unconfined_run_is_refused_without_running_the_cli() {
    let cli = FakeCli::ok("wrote out.png (1x1)\n");
    let value = serde_json::json!({ "input": "a.png", "output": "out.png" });
    let p: EditParams = serde_json::from_value(value).unwrap();
    let error = run::edit(&cli, &p).await.expect_err("an unconfined run is refused");

    assert!(error.to_string().contains("refusing an unconfined run"), "got: {error}");
    assert!(cli.never_ran());
}

/// The CLI's own `error:` lines are what the caller sees — never the whole stderr.
#[tokio::test]
async fn a_failing_run_reports_the_clis_own_error_lines() {
    let dir = tempfile::tempdir().unwrap();
    let cli = FakeCli::failing("reading a.png\nerror: no such file 'a.png'\n");
    let p = confined(dir.path(), serde_json::json!({ "input": "a.png" }));
    let error = run::edit(&cli, &p).await.expect_err("the run fails");

    assert_eq!(error.to_string(), "error: no such file 'a.png'");
}

/// Exit 0 with no `wrote` line is a contract breach, not a silent success.
#[tokio::test]
async fn success_without_a_wrote_line_is_an_error() {
    let dir = tempfile::tempdir().unwrap();
    let cli = FakeCli::ok("nothing to report\n");
    let p = confined(dir.path(), serde_json::json!({ "input": "a.png" }));
    let error = run::edit(&cli, &p).await.expect_err("a success with no wrote line fails");

    assert!(error.to_string().contains("printed no 'wrote' line"), "got: {error}");
}

/// Unless `overwrite`, the CLI is asked to refuse an existing output itself: it knows the name
/// it will write, extension filled in. Its refusal comes back with the way out.
#[tokio::test]
async fn without_overwrite_the_cli_is_asked_not_to_clobber() {
    let dir = tempfile::tempdir().unwrap();
    let cli = FakeCli::failing("error: --no-clobber: 'out.jpg' already exists\n");
    let p = confined(dir.path(), serde_json::json!({ "input": "shot.jpg", "output": "out" }));
    let error = run::edit(&cli, &p).await.expect_err("the CLI refused");

    let message = error.to_string();
    assert!(message.contains("'out.jpg' already exists"), "{message}");
    assert!(message.ends_with("pass overwrite=true to replace it"), "{message}");
    let argv = cli.argv();
    assert_eq!(argv[argv.len() - 3..], ["--no-clobber", "--confine-output", "out"]);
}

#[tokio::test]
async fn with_overwrite_the_flag_is_left_off() {
    let dir = tempfile::tempdir().unwrap();
    let cli = FakeCli::ok("wrote out.png (1x1)\n");
    let value = serde_json::json!({ "input": "a.png", "output": "out.png", "overwrite": true });
    run::edit(&cli, &confined(dir.path(), value)).await.expect("the run succeeds");
    assert!(!cli.argv().iter().any(|a| a == "--no-clobber"));
}

/// The operator's tokens ride the child's environment, one `origin=token` pair per server.
#[tokio::test]
async fn the_server_tokens_ride_the_childs_environment_not_argv() {
    let dir = tempfile::tempdir().unwrap();
    let cli = FakeCli::ok("wrote out.png (1x1)\n");
    let value = serde_json::json!({ "input": "a.png", "remote": "http://b.test", "output": "out.png" });
    let mut p = confined(dir.path(), value);
    p.server_tokens = Some("http://b.test=tok-b".into());
    run::edit(&cli, &p).await.expect("the run succeeds");

    let call = cli.call();
    let pair = ("STENCIL_SERVER_TOKENS".to_string(), "http://b.test=tok-b".to_string());
    assert_eq!(call.env, [pair]);
    assert!(!call.argv.iter().any(|a| a.contains("tok-b") || a == "--token"), "{:?}", call.argv);
}

/// An inline layout is materialized to a temp file whose path rides on `-l`, still there
/// while the CLI runs — the handle outlives the spawn. A JSON string is the same object.
#[tokio::test]
async fn an_inline_or_stringified_layout_rides_argv_as_a_temp_file() {
    let dir = tempfile::tempdir().unwrap();
    for layout in [serde_json::json!({ "lines": [] }), serde_json::json!("{\"lines\":[]}")] {
        let cli = FakeCli::ok("wrote out.png (2x2)\n");
        let value = serde_json::json!({ "input": "a.png", "layout": layout, "overwrite": true });
        run::edit(&cli, &confined(dir.path(), value)).await.expect("the run succeeds");

        let argv = cli.argv();
        let at = argv.iter().position(|a| a == "-l").expect("argv carries -l");
        assert!(argv[at + 1].ends_with(".json"), "got: {}", argv[at + 1]);
    }
}

#[tokio::test]
async fn a_stringified_layout_that_is_not_a_layout_is_refused() {
    let dir = tempfile::tempdir().unwrap();
    let cli = FakeCli::ok("wrote out.png (2x2)\n");
    let value = serde_json::json!({ "input": "a.png", "layout": "{\"lines\": 7}" });
    let error = run::edit(&cli, &confined(dir.path(), value)).await.expect_err("a bad layout");

    assert!(error.to_string().contains("not a layout JSON object"), "got: {error}");
    assert!(cli.never_ran());
}

/// A §2.1 `save` reports its project line, which carries no dimensions.
#[tokio::test]
async fn a_project_run_parses_the_clis_project_line() {
    let dir = tempfile::tempdir().unwrap();
    let cli = FakeCli::ok("wrote shot.stencil (project)\n");
    let p = confined(dir.path(), serde_json::json!({ "input": "a.png", "output": "shot.stencil" }));
    let path = run::project(&cli, &p).await.expect("the project run succeeds");

    assert_eq!(path, dir.path().join("shot.stencil").to_string_lossy());
}

/// A `stencil_edit` to a `.stencil` output is a success with no size, not a missing line.
#[tokio::test]
async fn an_edit_to_a_stencil_output_is_a_project_result() {
    let dir = tempfile::tempdir().unwrap();
    let cli = FakeCli::ok("wrote shot.stencil (project)\n");
    let p = confined(dir.path(), serde_json::json!({ "input": "a.png", "output": "shot.stencil" }));
    let result = run::edit(&cli, &p).await.expect("a project is a success");

    assert!(result.path.ends_with("shot.stencil"), "{}", result.path);
    assert_eq!((result.width, result.height), (None, None));
    assert_eq!(result.summary(), format!("wrote {} (project)", result.path));
}

/// A confined run is spawned INSIDE its root, its output rides relative under
/// `--confine-output`, and the reported path is joined back to the absolute one.
#[tokio::test]
async fn a_confined_run_spawns_in_the_root_and_rejoins_the_reported_path() {
    let dir = tempfile::tempdir().expect("a temp dir");
    let p = confined(dir.path(), serde_json::json!({ "input": "a.png", "output": "shot.png" }));

    let cli = FakeCli::ok("wrote shot.png (4x4)\n");
    let result = run::edit(&cli, &p).await.expect("the confined run succeeds");

    assert_eq!(result.path, dir.path().join("shot.png").to_string_lossy());
    let call = cli.call();
    assert_eq!(call.dir.as_deref(), Some(dir.path()));
    assert_eq!(call.argv[call.argv.len() - 2..], ["--confine-output", "shot.png"]);
    // Every other local path was made absolute before the working-directory change.
    assert!(Path::new(&call.argv[1]).is_absolute(), "input stayed relative: {}", call.argv[1]);
}
