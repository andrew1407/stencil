//! The opt-in preview run, without a CLI binary: `pipeline::preview::render` against the
//! recording runner — its argv, its confinement, the bytes it hands back, and the temp file
//! it leaves behind (none).

use stencil_mcp::pipeline::preview::{render, PREVIEW_SIDE};

mod common;
use common::cli::FakeCli;

/// Everything in `dir` but `keep`: what a preview left behind.
fn leftovers(dir: &std::path::Path, keep: &str) -> Vec<String> {
    let names = std::fs::read_dir(dir).unwrap().flatten();
    let names = names.map(|e| e.file_name().to_string_lossy().into_owned());
    names.filter(|n| n != keep).collect()
}

#[tokio::test]
async fn a_preview_is_a_confined_thumbnail_run_read_back_and_removed() {
    let dir = tempfile::tempdir().unwrap();
    let image = dir.path().join("out.png");
    std::fs::write(&image, b"the edit").unwrap();
    let wrote = "wrote .stencil-preview.png (512x384 px · A4 29.7×21cm)\n";
    let cli = FakeCli::rendering(wrote, b"PNG!");

    let root = dir.path().to_string_lossy();
    let preview = render(&cli, &image.to_string_lossy(), &root).await.expect("the preview renders");

    assert_eq!(preview.png, b"PNG!");
    assert_eq!((preview.width, preview.height), (512, 384));
    let call = cli.call();
    assert_eq!(call.dir.as_deref(), Some(dir.path()), "spawned inside the root");
    let argv = call.argv;
    let side = PREVIEW_SIDE.to_string();
    let head = ["-i", &*image.to_string_lossy(), "--thumbnail", &side, "--confine-output"];
    assert_eq!(argv[..5], head);
    let temp = &argv[5];
    assert!(temp.starts_with(".stencil-preview-") && temp.ends_with(".png"), "got: {temp}");
    assert_eq!(leftovers(dir.path(), "out.png"), Vec::<String>::new(), "the temp file is gone");
}

#[tokio::test]
async fn a_failed_preview_reports_the_clis_error_and_leaves_nothing() {
    let dir = tempfile::tempdir().unwrap();
    let cli = FakeCli::failing("error: could not decode an image from 'out.png' (BadImage)\n");

    let root = dir.path().to_string_lossy();
    let image = dir.path().join("out.png").to_string_lossy().into_owned();
    let error = render(&cli, &image, &root).await.err().expect("the preview fails");

    assert_eq!(error, "error: could not decode an image from 'out.png' (BadImage)");
    assert_eq!(leftovers(dir.path(), ""), Vec::<String>::new());
}

#[tokio::test]
async fn a_run_that_names_no_size_is_not_a_preview() {
    let dir = tempfile::tempdir().unwrap();
    let cli = FakeCli::rendering("nothing to report\n", b"PNG!");

    let root = dir.path().to_string_lossy();
    let error = render(&cli, "/elsewhere/out.png", &root).await.err().expect("no wrote line");

    assert!(error.contains("no 'wrote' line"), "got: {error}");
    assert_eq!(leftovers(dir.path(), ""), Vec::<String>::new());
}
