//! End-to-end edit runs against the real CLI; they self-skip when its binary is not found.
mod common;
use common::e2e::{cli_present, confined_params, FIXTURE};

use serde_json::json;
use stencil_mcp::{opplan, pipeline};

/// The CLI's `--probe` answers out of the header: size, format, alpha and the file's bytes.
#[tokio::test]
async fn probe_reports_fixture_dimensions() {
    if !cli_present() {
        return;
    }
    let argv = stencil_mcp::args::build_probe_argv(FIXTURE).unwrap();
    let doc = pipeline::run_probe(&argv).await.expect("probe should succeed");
    assert_eq!((doc["width"].as_u64(), doc["height"].as_u64()), (Some(16), Some(12)));
    assert_eq!(doc["format"], "png");
    let bytes = std::fs::metadata(FIXTURE).unwrap().len();
    assert_eq!(doc["bytes"].as_u64(), Some(bytes));
    assert!(doc.get("durationMs").is_none(), "a still carries no duration: {doc}");
}

/// `overwrite: false` is the CLI's own `--no-clobber`, checked on the name it would write:
/// `out` from a PNG input lands as `out.png`, so an existing `out.png` is refused.
#[tokio::test]
async fn refuses_to_clobber_the_auto_extended_name() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    std::fs::write(dir.path().join("out.png"), b"placeholder").unwrap();
    let call = json!({ "input": FIXTURE, "output": dir.path().join("out").to_string_lossy() });
    let err = pipeline::run_edit(&confined_params(call, dir.path())).await.unwrap_err().to_string();
    assert!(err.contains("out.png' already exists"), "got: {err}");
    assert!(err.contains("overwrite=true"), "got: {err}");
    assert_eq!(std::fs::read(dir.path().join("out.png")).unwrap(), b"placeholder");
}

#[tokio::test]
async fn edit_rotate_swaps_dimensions() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let out = dir.path().join("rotated.png");
    let params = confined_params(
        json!({ "input": FIXTURE, "rotate": 1, "output": out.to_string_lossy() }),
        dir.path(),
    );

    let result = pipeline::run_edit(&params)
        .await
        .expect("edit should succeed");
    assert_eq!((result.width, result.height), (Some(12), Some(16)));
    assert!(std::path::Path::new(&result.path).exists());
}

#[tokio::test]
async fn edit_crop_and_filter_with_inline_layout() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let out = dir.path().join("edited.png");
    let params = confined_params(
        json!({
            "input": FIXTURE,
            "crop": { "x1": 0, "x2": "50%" },
            "filter": "bw",
            "layout": {
                "lines": [ { "points": [ { "x": 0, "y": 0 }, { "x": 8, "y": 12 } ], "color": "#ff0000" } ]
            },
            "output": out.to_string_lossy(),
        }),
        dir.path(),
    );

    // The crop halves the width; exact pixel counts are the core's crop math, so we only
    // assert the wrapper passed the args through and parsed a sane, shrunken result.
    let result = pipeline::run_edit(&params)
        .await
        .expect("edit should succeed");
    let (width, height) = (result.width.unwrap(), result.height.unwrap());
    assert!(width < 16 && width > 0, "width = {width}");
    assert!(height > 0 && height <= 12, "height = {height}");
    assert!(std::path::Path::new(&result.path).exists());
}

#[tokio::test]
async fn refuses_to_clobber_without_overwrite() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let out = dir.path().join("exists.png");
    std::fs::write(&out, b"placeholder").unwrap();

    let call = json!({ "input": FIXTURE, "output": out.to_string_lossy() });
    let params = confined_params(call, dir.path());
    let err = pipeline::run_edit(&params).await.unwrap_err().to_string();
    assert!(err.contains("already exists"), "got: {err}");
}

/// The §2 widened forms through the real CLI: blank cm dims and page custom-cm-dims render
/// at the core's 96 dpi sizes, and the formula clear/disable forms are accepted-but-noted.
#[tokio::test]
async fn widened_blank_and_page_cm_dims_run_and_formula_clear_is_noted() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let out_dir = dir.path().to_string_lossy().into_owned();

    // blank with explicit cm dims (fixture 016's form): 10cm x 15cm → 378 x 567 px.
    let plan = opplan::parse_op_plan(
        r##"{"reply":"x","actions":[{"op":"blank","color":"white","width":10,"height":15}]}"##,
    )
    .await
    .unwrap();
    let requests = opplan::to_edit_requests(&plan, None, &out_dir, &mut Vec::new()).unwrap();
    let result = pipeline::run_edit(&requests[0].params).await.expect("blank dims run");
    assert_eq!((result.width, result.height), (Some(378), Some(567)));

    // page custom dims + blank (fixture 014's form): 20cm x 30cm → 756 x 1134 px.
    let plan = opplan::parse_op_plan(
        r##"{"reply":"x","actions":[{"op":"page","width":20,"height":30},{"op":"blank","color":"red"}]}"##,
    )
    .await
    .unwrap();
    let requests = opplan::to_edit_requests(&plan, None, &out_dir, &mut Vec::new()).unwrap();
    let result = pipeline::run_edit(&requests[0].params).await.expect("page dims run");
    assert_eq!((result.width, result.height), (Some(756), Some(1134)));

    // formula clear / disable (fixtures 011/012): valid, inert, noted — no CLI run.
    let plan = opplan::parse_op_plan(
        r##"{"reply":"x","actions":[{"op":"formula","axis":"y","expr":""},{"op":"formula","enabled":false}]}"##,
    )
    .await
    .unwrap();
    let mut notes = Vec::new();
    let requests = opplan::to_edit_requests(&plan, Some(FIXTURE), &out_dir, &mut notes).unwrap();
    assert!(requests.is_empty());
    assert!(notes.iter().any(|n| n.contains("formula")), "{notes:?}");
}

/// A §10 save `path` through the real CLI: the destination is honored relative to
/// output_dir (folder form) and the CLI bundles a real `.stencil` project there.
#[tokio::test]
async fn a_save_path_writes_the_project_inside_output_dir_via_the_real_cli() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let out_dir = dir.path().to_string_lossy().into_owned();

    let plan = opplan::parse_op_plan(
        r##"{"reply":"x","actions":[{"op":"rotate","dir":"right"},
            {"op":"save","name":"Keeper","path":"projects/best"}]}"##,
    )
    .await
    .unwrap();
    let mut notes = Vec::new();
    let requests = opplan::to_edit_requests(&plan, Some(FIXTURE), &out_dir, &mut notes).unwrap();
    assert!(notes.is_empty(), "{notes:?}");
    let save = requests.iter().find(|r| r.project).expect("a project write");
    let expected = dir.path().join("projects/best").join("keeper.stencil");
    assert_eq!(save.params.output, expected.to_string_lossy());

    // run_prompt creates each output's parent before running; mirror it here.
    std::fs::create_dir_all(expected.parent().unwrap()).unwrap();
    let path = pipeline::run_project(&save.params).await.expect("project write");
    assert_eq!(path, expected.to_string_lossy());
    assert!(expected.exists(), "the .stencil project landed at the honored path");
}

/// A layout path naming a file that is not a layout object reaches the caller as the CLI's
/// own `error:` line, never "the stencil CLI failed without a message".
#[tokio::test]
async fn a_non_object_layout_file_is_named_in_the_error() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let layout = dir.path().join("bad.json");
    std::fs::write(&layout, b"[]").unwrap();
    let call = json!({
        "input": FIXTURE,
        "layout": layout.to_string_lossy(),
        "output": dir.path().join("out.png").to_string_lossy(),
    });
    let err = pipeline::run_edit(&confined_params(call, dir.path())).await.unwrap_err().to_string();
    assert!(err.contains("could not read layout") && err.contains("(not a JSON object)"), "got: {err}");
    assert!(!err.contains("without a message"), "got: {err}");
}
