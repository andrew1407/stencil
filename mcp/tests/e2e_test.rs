//! End-to-end tests that drive the real CLI. They self-skip when the `stencil` binary
//! isn't built/findable, so `cargo test` stays green without a Zig toolchain present.

use serde_json::json;
use stencil_mcp::args::EditParams;
use stencil_mcp::{locate, pipeline};

use std::io::{Read, Write};
use std::net::TcpListener;

use stencil_mcp::config::LlmEnv;
use stencil_mcp::llm::{self, ChatMessage, LlmConfig, Role};
use stencil_mcp::llmtransport::PlainHttpTransport;
use stencil_mcp::opplan;

/// The 16x12 PNG fixture shared with the CLI's own test suite.
const FIXTURE: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../cli/tests/fixtures/sample.png"
);

fn cli_present() -> bool {
    if locate::find_cli().is_err() {
        eprintln!("skipping e2e: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return false;
    }
    true
}

fn edit_params(value: serde_json::Value) -> EditParams {
    serde_json::from_value(value).expect("params should deserialize")
}

/// A local PNG answers out of its own header; the CLI render is the fallback. The two must
/// agree — `edit_rotate_swaps_dimensions` below is the CLI's reading of the same fixture.
#[tokio::test]
async fn probe_reports_fixture_dimensions() {
    if !cli_present() {
        return;
    }
    let (w, h) = pipeline::run_probe(FIXTURE).await.expect("probe should succeed");
    assert_eq!((w, h), (16, 12));
}

#[tokio::test]
async fn edit_rotate_swaps_dimensions() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let out = dir.path().join("rotated.png");
    let params = edit_params(json!({
        "input": FIXTURE,
        "rotate": 1,
        "output": out.to_string_lossy(),
    }));

    let result = pipeline::run_edit(&params)
        .await
        .expect("edit should succeed");
    assert_eq!((result.width, result.height), (12, 16));
    assert!(std::path::Path::new(&result.path).exists());
}

#[tokio::test]
async fn edit_crop_and_filter_with_inline_layout() {
    if !cli_present() {
        return;
    }
    let dir = tempfile::tempdir().unwrap();
    let out = dir.path().join("edited.png");
    let params = edit_params(json!({
        "input": FIXTURE,
        "crop": { "x1": "0", "x2": "50%" },
        "filter": "bw",
        "layout": {
            "lines": [ { "points": [ { "x": 0, "y": 0 }, { "x": 8, "y": 12 } ], "color": "#ff0000" } ]
        },
        "output": out.to_string_lossy(),
    }));

    // The crop halves the width; exact pixel counts are the core's crop math, so we only
    // assert the wrapper passed the args through and parsed a sane, shrunken result.
    let result = pipeline::run_edit(&params)
        .await
        .expect("edit should succeed");
    assert!(
        result.width < 16 && result.width > 0,
        "width = {}",
        result.width
    );
    assert!(
        result.height > 0 && result.height <= 12,
        "height = {}",
        result.height
    );
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

    let params = edit_params(json!({
        "input": FIXTURE,
        "output": out.to_string_lossy(),
    }));
    let err = pipeline::run_edit(&params).await.unwrap_err().to_string();
    assert!(err.contains("already exists"), "got: {err}");
}

/// The §2 widened forms end-to-end through the real CLI: blank cm dims and a page
/// custom-cm-dims + blank plan render at the core's 96 dpi pixel sizes, and the formula
/// clear/disable forms are accepted-but-noted (zero CLI runs).
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
    .unwrap();
    let requests = opplan::to_edit_requests(&plan, None, &out_dir, &mut Vec::new()).unwrap();
    let result = pipeline::run_edit(&requests[0].params).await.expect("blank dims run");
    assert_eq!((result.width, result.height), (378, 567));

    // page custom dims + blank (fixture 014's form): 20cm x 30cm → 756 x 1134 px.
    let plan = opplan::parse_op_plan(
        r##"{"reply":"x","actions":[{"op":"page","width":20,"height":30},{"op":"blank","color":"red"}]}"##,
    )
    .unwrap();
    let requests = opplan::to_edit_requests(&plan, None, &out_dir, &mut Vec::new()).unwrap();
    let result = pipeline::run_edit(&requests[0].params).await.expect("page dims run");
    assert_eq!((result.width, result.height), (756, 1134));

    // formula clear / disable (fixtures 011/012): valid, inert, noted — no CLI run.
    let plan = opplan::parse_op_plan(
        r##"{"reply":"x","actions":[{"op":"formula","axis":"y","expr":""},{"op":"formula","enabled":false}]}"##,
    )
    .unwrap();
    let mut notes = Vec::new();
    let requests = opplan::to_edit_requests(&plan, Some(FIXTURE), &out_dir, &mut notes).unwrap();
    assert!(requests.is_empty());
    assert!(notes.iter().any(|n| n.contains("formula")), "{notes:?}");
}

/// A §10 save `path` end-to-end through the real CLI: the destination is honored
/// relative to output_dir (folder form), the parent is created the way `run_prompt`
/// does, and the CLI bundles a real `.stencil` project there.
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

/// §7 edge map: the CLI's contour filter renders the input into an attachable PNG.
#[tokio::test]
async fn edge_map_renders_the_contour_filter_via_the_cli() {
    if !cli_present() {
        return;
    }
    let bytes = pipeline::render_edge_map(FIXTURE)
        .await
        .expect("the contour render succeeds");
    assert!(bytes.starts_with(&[0x89, b'P', b'N', b'G']), "a PNG comes back");
    let attachment = llm::edge_map_attachment(&bytes).expect("within the 8 MiB cap");
    assert_eq!(attachment.media_type, "image/png");
}

/// A source the CLI cannot render (a missing file) silently yields no edge map.
#[tokio::test]
async fn edge_map_render_failure_is_a_silent_none() {
    if !cli_present() {
        return;
    }
    assert!(pipeline::render_edge_map("/nonexistent/input.png").await.is_none());
}

/// The full `stencil_prompt` flow against the real CLI and a canned local "ollama": chat
/// over the plain-http transport → parse the op-plan → map to CLI runs → execute. The
/// canned LLM is a one-shot TcpListener this test starts itself; the test still self-skips
/// without the CLI binary.
#[tokio::test]
async fn prompt_flow_against_a_canned_llm_and_the_real_cli() {
    if !cli_present() {
        return;
    }

    // The plan the "model" answers with: rotate right, plus one sepia variant.
    let plan_json = json!({
        "version": 1,
        "reply": "Rotated it; the sepia take is separate.",
        "actions": [{"op": "rotate", "dir": "right"}],
        "variants": [{"label": "Sepia Tone", "actions": [{"op": "filter", "mode": "sepia"}]}]
    })
    .to_string();
    let ollama_body = json!({"message": {"role": "assistant", "content": plan_json}}).to_string();

    // A one-shot canned ollama endpoint on an ephemeral port.
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind an ephemeral port");
    let base_url = format!("http://{}", listener.local_addr().unwrap());
    std::thread::spawn(move || {
        let (mut stream, _) = listener.accept().expect("accept");
        let mut buf = [0u8; 65536];
        let mut request: Vec<u8> = Vec::new();
        // Read until the request body (Content-Length arithmetic is overkill here: the
        // client writes the whole request before reading, so read until the JSON closes).
        loop {
            let n = stream.read(&mut buf).expect("read request");
            request.extend_from_slice(&buf[..n]);
            if n == 0 || request.ends_with(b"}") {
                break;
            }
        }
        let head = String::from_utf8_lossy(&request);
        assert!(head.starts_with("POST /api/chat HTTP/1.1\r\n"), "{head}");
        let response = format!(
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\n\r\n{}",
            ollama_body.len(),
            ollama_body
        );
        stream.write_all(response.as_bytes()).expect("write response");
    });

    // 1. Chat through the real plain-http transport.
    let env = LlmEnv {
        provider: Some("ollama".into()),
        base_url: Some(base_url),
        ..LlmEnv::default()
    };
    let config = LlmConfig::resolve(&env, Some("test-model")).unwrap();
    let messages = vec![ChatMessage {
        role: Role::User,
        text: "rotate it right and give me a sepia variant".to_string(),
        images: Vec::new(),
    }];
    let reply = tokio::task::spawn_blocking(move || {
        llm::chat(&PlainHttpTransport::new(), &config, &messages, "")
    })
    .await
    .unwrap()
    .expect("chat should succeed");

    // 2. Parse and map the plan.
    let plan = opplan::parse_op_plan(&reply).expect("plan should validate");
    assert_eq!(plan.reply, "Rotated it; the sepia take is separate.");
    let dir = tempfile::tempdir().unwrap();
    let out_dir = dir.path().to_string_lossy().into_owned();
    let requests = opplan::to_edit_requests(
        &plan,
        Some(FIXTURE),
        &out_dir,
        &mut Vec::new(),
    ).expect("mappable");
    assert_eq!(requests.len(), 2);

    // 3. Execute through the real CLI: base = rotated (dims swap), variant = rotated+sepia.
    let base = pipeline::run_edit(&requests[0].params)
        .await
        .expect("base edit should succeed");
    assert_eq!((base.width, base.height), (12, 16));
    assert!(base.path.ends_with("result.png"), "{}", base.path);

    let variant = pipeline::run_edit(&requests[1].params)
        .await
        .expect("variant edit should succeed");
    assert_eq!((variant.width, variant.height), (12, 16));
    assert!(variant.path.ends_with("sepia-tone.png"), "{}", variant.path);
    assert!(std::path::Path::new(&variant.path).exists());
}
