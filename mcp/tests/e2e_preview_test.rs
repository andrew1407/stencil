//! The opt-in `preview` against the real CLI, through the real handler: the attached block is
//! a PNG the CLI's `--thumbnail` fitted into 512 px. Self-skips without the binary.
mod common;
use common::dispatch::{payload_of, text_of, wire, Harness};
use common::e2e::{cli_present, FIXTURE};

use base64::Engine;
use serde_json::{json, Value};

/// The width and height a base64 PNG block declares in its IHDR chunk.
fn png_size(block: &Value) -> (u32, u32) {
    let data = block["data"].as_str().expect("an image block's data");
    let png = base64::engine::general_purpose::STANDARD.decode(data).expect("base64");
    assert_eq!(&png[..8], b"\x89PNG\r\n\x1a\n", "a PNG signature");
    let be = |at: usize| u32::from_be_bytes(png[at..at + 4].try_into().unwrap());
    (be(16), be(20))
}

#[tokio::test]
async fn an_edit_preview_is_the_result_fitted_into_512_px() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let blank = json!({ "width": 1024, "height": 600, "color": "teal" });
    let call = json!({ "blank": blank, "output": "page.png", "preview": true });
    let result = Harness::rooted(dir.path()).call("stencil_edit", call).await.unwrap();

    let wire = wire(&result);
    assert_eq!(wire["isError"], false, "{}", text_of(&result));
    let blocks = wire["content"].as_array().unwrap();
    assert_eq!(blocks.len(), 3);
    assert_eq!((&blocks[2]["type"], &blocks[2]["mimeType"]), (&json!("image"), &json!("image/png")));
    assert_eq!(png_size(&blocks[2]), (512, 300));
    let payload = payload_of(&result);
    assert_eq!((&payload["width"], &payload["height"]), (&json!(1024), &json!(600)));
    let page = dir.path().join("page.png").to_string_lossy().into_owned();
    assert_eq!(payload["preview"], json!({ "path": page, "width": 512, "height": 300 }));
    let entries = std::fs::read_dir(dir.path()).unwrap().flatten();
    let left: Vec<_> = entries.map(|e| e.file_name()).collect();
    assert_eq!(left, ["page.png"], "only the result stays in the root");
}

#[tokio::test]
async fn a_script_preview_keeps_a_small_image_at_its_own_size() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let call = json!({
        "script_text": "@save shot.png\n", "input": FIXTURE, "output_dir": dir.path(), "preview": true,
    });
    let result = Harness::rooted(dir.path()).call("stencil_script", call).await.unwrap();

    let wire = wire(&result);
    assert_eq!(wire["isError"], false, "{}", text_of(&result));
    let blocks = wire["content"].as_array().unwrap();
    assert_eq!(blocks.len(), 3);
    assert_eq!(png_size(&blocks[2]), (16, 12), "a 16x12 image is never enlarged");
    assert_eq!(payload_of(&result)["previews"][0]["width"], 16);
}
