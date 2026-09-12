//! The whole edit / project / scrape run, without a CLI binary. `pipeline::run` is generic
//! over `CliRunner`, so everything AROUND the spawn — the clobber guard, the inline-layout
//! temp file, the argv, the confinement rewrite, the outcome parsing — runs here against the
//! recording runner in `common::cli`. Probe and edge map: `pipeline_probe_test.rs`.

use std::path::Path;

use stencil_mcp::args::EditParams;
use stencil_mcp::pipeline::run;

mod common;
use common::cli::FakeCli;

fn params(value: serde_json::Value) -> EditParams {
    serde_json::from_value(value).expect("params should deserialize")
}

#[tokio::test]
async fn a_successful_run_becomes_the_wrote_line_and_its_dimensions() {
    let cli = FakeCli::ok("wrote /tmp/out.png (800x600)\n");
    let result = run::edit(&cli, &params(serde_json::json!({ "input": "a.png", "output": "/tmp/out.png" })))
        .await
        .expect("the run succeeds");

    assert_eq!(result.path, "/tmp/out.png");
    assert_eq!((result.width, result.height), (800, 600));
    assert_eq!(cli.argv(), ["-i", "a.png", "/tmp/out.png"]);
}

/// The CLI's own `error:` lines are what the caller sees — never the whole stderr.
#[tokio::test]
async fn a_failing_run_reports_the_clis_own_error_lines() {
    let cli = FakeCli::failing("reading a.png\nerror: no such file 'a.png'\n");
    let error = run::edit(&cli, &params(serde_json::json!({ "input": "a.png", "output": "out.png" })))
        .await
        .expect_err("the run fails");

    assert_eq!(error.to_string(), "error: no such file 'a.png'");
}

/// Exit 0 with no `wrote` line is a contract breach, not a silent success.
#[tokio::test]
async fn success_without_a_wrote_line_is_an_error() {
    let cli = FakeCli::ok("nothing to report\n");
    let error = run::edit(&cli, &params(serde_json::json!({ "input": "a.png", "output": "out.png" })))
        .await
        .expect_err("a success with no wrote line fails");

    assert!(error.to_string().contains("printed no 'wrote' line"), "got: {error}");
}

/// The clobber guard runs BEFORE the spawn: an existing output costs no CLI run at all.
#[tokio::test]
async fn the_clobber_guard_refuses_an_existing_output_without_running_the_cli() {
    let existing = tempfile::NamedTempFile::new().expect("a temp file");
    let output = existing.path().to_string_lossy().into_owned();
    let cli = FakeCli::ok("wrote x (1x1)\n");

    let error = run::edit(&cli, &params(serde_json::json!({ "input": "a.png", "output": output })))
        .await
        .expect_err("an existing output is refused");

    assert!(error.to_string().contains("already exists"), "got: {error}");
    assert!(cli.never_ran(), "the CLI must not run");
}

/// An inline layout is materialized to a temp file whose path rides on `-l`, still there
/// while the CLI runs — the handle outlives the spawn.
#[tokio::test]
async fn an_inline_layout_rides_argv_as_a_temp_file_that_exists_during_the_run() {
    let cli = FakeCli::ok("wrote out.png (2x2)\n");
    run::edit(
        &cli,
        &params(serde_json::json!({
            "input": "a.png",
            "output": "out.png",
            "layout": { "lines": [] },
        })),
    )
    .await
    .expect("the run succeeds");

    let argv = cli.argv();
    let at = argv.iter().position(|a| a == "-l").expect("argv carries -l");
    assert!(argv[at + 1].ends_with(".json"), "got: {}", argv[at + 1]);
}

/// A §2.1 `save` reports its project line, which carries no dimensions.
#[tokio::test]
async fn a_project_run_parses_the_clis_project_line() {
    let cli = FakeCli::ok("wrote /tmp/shot.stencil (project)\n");
    let path = run::project(&cli, &params(serde_json::json!({ "input": "a.png", "output": "/tmp/shot.stencil" })))
        .await
        .expect("the project run succeeds");

    assert_eq!(path, "/tmp/shot.stencil");
}

/// A confined run is spawned INSIDE its root, its output rides relative under
/// `--confine-output`, and the reported path is joined back to the absolute one.
#[tokio::test]
async fn a_confined_run_spawns_in_the_root_and_rejoins_the_reported_path() {
    let dir = tempfile::tempdir().expect("a temp dir");
    let out = dir.path().join("shot.png");
    let mut p = params(serde_json::json!({ "input": "a.png", "output": out.to_string_lossy() }));
    p.confine_root = Some(dir.path().to_string_lossy().into_owned());

    let cli = FakeCli::ok("wrote shot.png (4x4)\n");
    let result = run::edit(&cli, &p).await.expect("the confined run succeeds");

    assert_eq!(result.path, out.to_string_lossy());
    let call = cli.call();
    assert_eq!(call.dir.as_deref(), Some(dir.path()));
    assert_eq!(call.argv[call.argv.len() - 2..], ["--confine-output", "shot.png"]);
    // Every other local path was made absolute before the working-directory change.
    assert!(Path::new(&call.argv[1]).is_absolute(), "input stayed relative: {}", call.argv[1]);
}

/// A scrape's success path: the CLI's per-file `wrote` lines and its summary line become
/// the structured result.
#[tokio::test]
async fn a_scrape_reports_every_file_the_cli_wrote() {
    let cli = FakeCli::ok(
        "wrote out/a.png (10x10 px · source x.test)\n\
         wrote out/b.png (20x30 px · source x.test)\n\
         scraped 2 file(s) from x.test into out\n",
    );
    let params: stencil_mcp::args::ScrapeParams =
        serde_json::from_value(serde_json::json!({ "source_site": "http://x.test" })).unwrap();

    let result = run::scrape(&cli, &params).await.expect("the scrape succeeds");

    assert_eq!(result.dir.as_deref(), Some("out"));
    assert_eq!(result.host.as_deref(), Some("x.test"));
    let paths: Vec<&str> = result.files.iter().map(|f| f.path.as_str()).collect();
    assert_eq!(paths, ["out/a.png", "out/b.png"]);
    assert_eq!((result.files[1].width, result.files[1].height), (Some(20), Some(30)));
}

/// A scrape that matched nothing must not report an empty success.
#[tokio::test]
async fn a_scrape_that_wrote_no_files_is_an_error() {
    let cli = FakeCli::ok("scraped 0 file(s) from example.com\n");
    let params: stencil_mcp::args::ScrapeParams =
        serde_json::from_value(serde_json::json!({ "source_site": "http://example.com" })).unwrap();

    let error = run::scrape(&cli, &params).await.expect_err("an empty scrape fails");
    assert!(error.to_string().contains("wrote no files"), "got: {error}");
}
