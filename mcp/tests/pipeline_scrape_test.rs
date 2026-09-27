//! A whole `source_site` scrape without a CLI binary: the confinement into the call's root,
//! the spawn directory, and the per-file outcome parsing, against the recording runner.

use serde_json::json;
use stencil_mcp::args::ScrapeParams;
use stencil_mcp::pipeline::run;

mod common;
use common::cli::FakeCli;

fn params(value: serde_json::Value) -> ScrapeParams {
    serde_json::from_value(value).expect("scrape params should deserialize")
}

/// The scrape is spawned inside its root with `--confine-output`, its directory relative to
/// that root, and every path the CLI reports comes back absolute.
#[tokio::test]
async fn a_scrape_is_confined_to_its_root_and_reports_every_file() {
    let root = tempfile::tempdir().unwrap();
    let out = root.path().join("shots");
    let cli = FakeCli::ok(
        "scraping http://x.test…\n\
         wrote shots/a.png (10x10 px · source x.test)\n\
         wrote shots/b.png (20x30 px · source x.test)\n\
         scraped 2 file(s) from x.test into shots\n",
    );
    let p = params(json!({ "source_site": "http://x.test", "output": out }));

    let result = run::scrape(&cli, &p, &root.path().to_string_lossy()).await.expect("it runs");

    assert_eq!(result.dir.as_deref(), Some(&*out.to_string_lossy()));
    assert_eq!(result.host.as_deref(), Some("x.test"));
    let paths: Vec<&str> = result.files.iter().map(|f| f.path.as_str()).collect();
    assert_eq!(paths, [out.join("a.png").to_string_lossy(), out.join("b.png").to_string_lossy()]);
    assert_eq!((result.files[1].width, result.files[1].height), (Some(20), Some(30)));

    let call = cli.call();
    assert_eq!(call.dir.as_deref(), Some(root.path()));
    assert_eq!(call.argv[call.argv.len() - 2..], ["--confine-output", "shots"]);
}

/// The root itself is a valid destination: it rides as `.`.
#[tokio::test]
async fn a_scrape_into_the_root_itself_rides_as_the_current_directory() {
    let root = tempfile::tempdir().unwrap();
    let cli = FakeCli::ok("wrote a.png (1x1 px · source x.test)\n");
    let p = params(json!({ "source_site": "http://x.test", "output": root.path() }));

    run::scrape(&cli, &p, &root.path().to_string_lossy()).await.expect("it runs");
    let argv = cli.argv();
    assert_eq!(argv[argv.len() - 2..], ["--confine-output", "."]);
}

/// A destination outside the root never reaches the CLI.
#[tokio::test]
async fn a_scrape_outside_its_root_is_refused_before_the_cli_runs() {
    let root = tempfile::tempdir().unwrap();
    let cli = FakeCli::ok("wrote a.png (1x1)\n");
    let p = params(json!({ "source_site": "http://x.test", "output": "/elsewhere/shots" }));

    let error = run::scrape(&cli, &p, &root.path().to_string_lossy()).await.expect_err("refused");
    assert!(error.to_string().contains("refusing to write outside"), "got: {error}");
    assert!(cli.never_ran());
}

/// A scrape that matched nothing must not report an empty success.
#[tokio::test]
async fn a_scrape_that_wrote_no_files_is_an_error() {
    let root = tempfile::tempdir().unwrap();
    let cli = FakeCli::ok("scraped 0 file(s) from example.com\n");
    let p = params(json!({ "source_site": "http://example.com", "output": root.path() }));

    let error = run::scrape(&cli, &p, &root.path().to_string_lossy()).await.expect_err("empty");
    assert!(error.to_string().contains("wrote no files"), "got: {error}");
}
