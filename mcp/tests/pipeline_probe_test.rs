//! The two runs that render to a throwaway file — `stencil_probe`'s fallback and the §7 edge
//! map — driven against the recording runner in `common::cli` rather than the real binary.

use stencil_mcp::pipeline::run;

mod common;
use common::cli::FakeCli;

/// An input with no readable local header (a URL, a video) falls back to a throwaway render,
/// and the dimensions come from that run's `wrote` line.
#[tokio::test]
async fn a_probe_with_no_readable_header_renders_through_the_cli() {
    let cli = FakeCli::ok("wrote /tmp/probe.png (320x240)\n");
    let dims = run::probe(&cli, "https://example.test/clip.mp4").await.expect("the probe succeeds");

    assert_eq!(dims, (320, 240));
    let argv = cli.argv();
    assert_eq!(argv[..2], ["-i", "https://example.test/clip.mp4"]);
    assert!(argv[2].ends_with(".png"), "renders to a temp png: {}", argv[2]);
}

#[tokio::test]
async fn a_probe_whose_render_prints_no_wrote_line_fails() {
    let cli = FakeCli::ok("nothing to report\n");
    let error = run::probe(&cli, "https://example.test/clip.mp4").await.expect_err("no dimensions");
    assert!(error.contains("could not determine the image dimensions"), "got: {error}");
}

/// A readable local header answers without the CLI at all.
#[tokio::test]
async fn a_probe_of_a_local_png_never_runs_the_cli() {
    let png = concat!(env!("CARGO_MANIFEST_DIR"), "/../cli/tests/fixtures/sample.png");
    let cli = FakeCli::ok("wrote /tmp/probe.png (1x1)\n");
    assert_eq!(run::probe(&cli, png).await.expect("the header answers"), (16, 12));
    assert!(cli.never_ran(), "the CLI must not run");
}

/// The §7 edge map is the contour render's bytes, read back off the temp file.
#[tokio::test]
async fn the_edge_map_is_the_bytes_of_a_contour_render() {
    let cli = FakeCli::rendering("wrote /tmp/edge.png (4x4)\n", b"\x89PNG\r\n edges");
    let bytes = run::edge_map(&cli, "a.png").await.expect("an edge map");

    assert_eq!(bytes, b"\x89PNG\r\n edges");
    assert_eq!(cli.argv()[..4], ["-i", "a.png", "--filter", "contour"]);
}

/// A failed render is best-effort: no edge map, no error.
#[tokio::test]
async fn a_failed_contour_render_yields_no_edge_map() {
    let cli = FakeCli::failing("error: cannot read 'a.png'\n");
    assert!(run::edge_map(&cli, "a.png").await.is_none());
}
