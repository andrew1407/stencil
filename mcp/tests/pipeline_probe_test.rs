//! The CLI's reporting modes and the §7 edge map, driven against the recording runner in
//! `common::cli` rather than the real binary: `--probe` and the project reads answer on
//! stdout, and a project read hands the server's token over in the child's environment.

use stencil_mcp::args::{build_probe_argv, build_projects_argv};
use stencil_mcp::pipeline::{inspect, run, SERVER_TOKENS};

mod common;
use common::cli::FakeCli;

#[tokio::test]
async fn a_probe_is_the_clis_json_document() {
    let doc = r#"{"format":"mp4","width":320,"height":240,"alpha":null,"bytes":9,"durationMs":1000,"frames":30}"#;
    let cli = FakeCli::printing(true, &format!("{doc}\n"), "");
    let argv = build_probe_argv("/abs/clip.mp4").unwrap();
    let value = inspect::probe(&cli, &argv).await.expect("the probe succeeds");

    assert_eq!(value["width"], 320);
    assert_eq!(value["durationMs"], 1000);
    assert_eq!(cli.argv(), ["--probe", "-i", "/abs/clip.mp4"]);
}

#[tokio::test]
async fn a_failed_probe_is_the_clis_own_error_line() {
    let cli = FakeCli::printing(false, "", "error: could not read an image header from 'a.txt'\n");
    let argv = build_probe_argv("a.txt").unwrap();
    let error = inspect::probe(&cli, &argv).await.expect_err("no header");
    assert_eq!(error, "error: could not read an image header from 'a.txt'");
    assert!(build_probe_argv("-rf").is_err(), "a dash-leading input would parse as a flag");
}

/// The token rides the child's environment, never argv — a `ps` listing shows no secret.
#[tokio::test]
async fn a_project_read_hands_the_token_over_in_the_environment() {
    let cli = FakeCli::printing(true, "[{\"id\":\"p_1_a\",\"name\":\"Plans\"}]\n", "");
    let argv = build_projects_argv("http://localhost:8090", None).unwrap();
    let tokens = Some("http://localhost:8090=tok");
    let listing = inspect::projects(&cli, &argv, tokens).await.expect("a listing");

    assert_eq!(listing[0]["name"], "Plans");
    let call = cli.call();
    assert_eq!(call.argv, ["--server", "http://localhost:8090", "--list-projects"]);
    assert_eq!(call.env, [(SERVER_TOKENS.to_string(), "http://localhost:8090=tok".to_string())]);
    assert!(!call.argv.iter().any(|a| a.contains("tok")), "a token reached argv");

    let one = build_projects_argv("http://localhost:8090", Some("p_1_a")).unwrap();
    assert_eq!(one[2..], ["--project-info", "p_1_a"]);
}

#[tokio::test]
async fn stdout_that_is_no_json_document_is_an_error() {
    let cli = FakeCli::printing(true, "not json\n", "");
    let argv = build_projects_argv("http://h", None).unwrap();
    let error = inspect::projects(&cli, &argv, None).await.expect_err("no document");
    assert!(error.contains("printed no JSON document"), "{error}");
    assert!(cli.call().env.is_empty(), "no token, no variable");
}

/// The §7 edge map is the contour render's bytes, read back off a file the run wrote confined
/// inside a private temp directory, which is gone once they are read.
#[tokio::test]
async fn the_edge_map_is_the_bytes_of_a_contour_render() {
    let cli = FakeCli::rendering("wrote edge-map.png (4x4)\n", b"\x89PNG\r\n edges");
    let bytes = run::edge_map(&cli, "/elsewhere/a.png").await.expect("an edge map");

    assert_eq!(bytes, b"\x89PNG\r\n edges");
    let call = cli.call();
    let head = ["-i", "/elsewhere/a.png", "--filter", "contour", "--confine-output", "edge-map.png"];
    assert_eq!(call.argv, head);
    let dir = call.dir.expect("spawned inside its scratch directory");
    assert!(dir.file_name().unwrap().to_string_lossy().starts_with("stencil-edgemap-"));
    assert!(!dir.exists(), "the scratch directory is gone");
}

/// A failed render is best-effort: no edge map, no error.
#[tokio::test]
async fn a_failed_contour_render_yields_no_edge_map() {
    let cli = FakeCli::failing("error: cannot read 'a.png'\n");
    assert!(run::edge_map(&cli, "a.png").await.is_none());
}
