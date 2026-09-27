//! The opt-in `preview` through the real `tools/call` handler, a `/bin/sh` stub standing in for
//! the CLI (so this binary sets `STENCIL_CLI`): an edit renders "EDIT", a `--thumbnail` run
//! "PNG!", a script five files. Off, the result is exactly what it was without the parameter.
#![cfg(unix)]

mod common;
use common::dispatch::{payload_of, text_of, wire, Harness};

use std::path::{Path, PathBuf};
use std::sync::LazyLock;

use serde_json::json;

const STUB: &str = r#"#!/bin/sh
for last; do :; done
case " $* " in
*" --thumbnail "*) printf 'PNG!' > "$last"; echo "wrote $last (8x6 px · A4 29.7×21cm)" >&2 ;;
*" --script "*) for n in 1 2 3 4 5; do : > "s$n.png"; echo "wrote s$n.png (16x12 px · A4 29.7×21cm)" >&2; done ;;
*".stencil ") : > "$last"; echo "wrote $last (project)" >&2 ;;
*) printf 'EDIT' > "$last"; echo "wrote $last (16x12 px · A4 29.7×21cm)" >&2 ;;
esac
"#;

/// The stub, written once for every test here; its directory lives as long as the binary.
static CLI: LazyLock<(tempfile::TempDir, PathBuf)> = LazyLock::new(|| {
    use std::os::unix::fs::PermissionsExt;
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("stencil-stub");
    std::fs::write(&path, STUB).expect("write the stub");
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o755)).expect("chmod +x");
    std::env::set_var("STENCIL_CLI", &path);
    (dir, path)
});

fn rooted(dir: &Path) -> Harness {
    LazyLock::force(&CLI);
    Harness::rooted(dir)
}

fn names(dir: &Path) -> Vec<String> {
    let entries = std::fs::read_dir(dir).unwrap().flatten();
    let mut names: Vec<String> = entries.map(|e| e.file_name().to_string_lossy().into_owned()).collect();
    names.sort();
    names
}

#[tokio::test]
async fn an_edit_preview_is_one_png_block_named_in_the_payload() {
    let dir = tempfile::tempdir().unwrap();
    let call = json!({ "input": "in.png", "output": "out.png", "preview": true });
    let result = rooted(dir.path()).call("stencil_edit", call).await.unwrap();

    let wire = wire(&result);
    assert_eq!(wire["content"].as_array().unwrap().len(), 3, "{wire}");
    assert_eq!(wire["content"][2]["type"], "image");
    assert_eq!(wire["content"][2]["mimeType"], "image/png");
    assert_eq!(wire["content"][2]["data"], "UE5HIQ==", "base64 of the rendered PNG!");
    let out = dir.path().join("out.png").to_string_lossy().into_owned();
    let note = json!({ "path": out, "width": 8, "height": 6 });
    assert_eq!(payload_of(&result)["preview"], note);
    assert_eq!(wire["structuredContent"]["preview"], note);
    assert!(text_of(&result).ends_with(&format!("\npreview: {out} (8x6) attached")));
    assert_eq!(names(dir.path()), ["out.png"], "the preview's temp file is gone");
}

#[tokio::test]
async fn without_preview_the_result_is_unchanged() {
    let dir = tempfile::tempdir().unwrap();
    let h = rooted(dir.path());
    let call = json!({ "input": "in.png", "output": "out.png", "overwrite": true });
    let omitted = wire(&h.call("stencil_edit", call.clone()).await.unwrap());
    let mut off = call;
    off["preview"] = json!(false);
    let off = wire(&h.call("stencil_edit", off).await.unwrap());

    assert_eq!(omitted, off);
    assert_eq!(omitted["content"].as_array().unwrap().len(), 2);
    assert!(omitted["structuredContent"].get("preview").is_none(), "{omitted}");
    let out = dir.path().join("out.png");
    assert_eq!(omitted["content"][0]["text"], format!("wrote {} (16x12)", out.display()));
}

#[tokio::test]
async fn a_project_has_no_preview_and_says_so() {
    let dir = tempfile::tempdir().unwrap();
    let call = json!({ "input": "in.png", "output": "p.stencil", "preview": true });
    let result = rooted(dir.path()).call("stencil_edit", call).await.unwrap();

    assert_eq!(wire(&result)["content"].as_array().unwrap().len(), 2);
    assert!(text_of(&result).ends_with("\npreview: none for a .stencil project"));
    assert!(payload_of(&result).get("preview").is_none());
}

#[tokio::test]
async fn a_script_previews_its_first_images_up_to_the_cap() {
    let dir = tempfile::tempdir().unwrap();
    let call = json!({ "script_text": "@save x\n", "output_dir": dir.path(), "preview": true });
    let result = rooted(dir.path()).call("stencil_script", call).await.unwrap();

    let shown = wire(&result);
    let blocks = shown["content"].as_array().unwrap();
    assert_eq!(blocks.len(), 2 + 4, "four previews of five files");
    assert!(blocks[2..].iter().all(|b| b["type"] == "image" && b["data"] == "UE5HIQ=="));
    let previews = payload_of(&result)["previews"].as_array().unwrap().clone();
    let s1 = dir.path().join("s1.png").to_string_lossy().into_owned();
    assert_eq!(previews[0], json!({ "path": s1, "width": 8, "height": 6 }));
    assert_eq!(previews.len(), 4);
    assert!(text_of(&result).ends_with("\npreview: the first 4 of 5 images"));

    let plain = json!({ "script_text": "@save x\n", "output_dir": dir.path() });
    let plain = rooted(dir.path()).call("stencil_script", plain).await.unwrap();
    assert_eq!(wire(&plain)["content"].as_array().unwrap().len(), 2);
    assert!(payload_of(&plain).get("previews").is_none());
}
