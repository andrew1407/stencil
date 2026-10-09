//! `parse_op_plan` over a recording runner: the reply rides on stdin to `--plan-check - --plan-surface
//! mcp`, the envelope is read, a CLI built from another registry is refused, never trusted, and
//! a refusal reads in core's canonical words.


use crate::common::cli::FakeCli;
use crate::common::try_plan;
use stencil_mcp::opplan::{parse_op_plan_with, registry_skew, Action, Dir, OpPlanError};
use stencil_mcp::registry::fingerprint;

/// The one line `--plan-check` prints, around `result`, for this server's own registry.
fn envelope(result: &str) -> String {
    let (bytes, fnv) = fingerprint();
    format!(
        r#"{{"version":1,"surface":"mcp","registryBytes":{bytes},"registryFnv1a64":"{fnv}","result":{result}}}"#
    )
}

const ROTATED: &str = r#"{"status":"valid","reply":"ok","actions":[{"op":"rotate","dir":"left","times":1}],"variants":[],"ask":null,"warnings":[],"error":null}"#;

#[tokio::test]
async fn the_reply_rides_on_stdin_and_the_result_is_typed() {
    let cli = FakeCli::printing(true, &(envelope(ROTATED) + "\n"), "");
    let reply = "Sure:\n```json\n{\"reply\":\"ok\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\"}]}\n```";

    let plan = parse_op_plan_with(&cli, reply).await.unwrap();

    assert_eq!(plan.actions, [Action::Rotate { dir: Dir::Left, times: 1 }]);
    let call = cli.call();
    assert_eq!(call.argv, ["--plan-check", "-", "--plan-surface", "mcp"]);
    assert_eq!(call.input.as_deref(), Some(reply), "the reply goes verbatim, prose and fences too");
}

#[tokio::test]
async fn an_invalid_plan_is_its_error_even_though_the_cli_exits_one() {
    let invalid = r#"{"status":"invalid","reply":"","actions":[],"variants":[],"ask":null,"warnings":[],"error":{"code":"E_ACTION","op":"rotate","detail":"\"dir\" must be one of \"left\", \"right\"","message":"Invalid rotate action: \"dir\" must be one of \"left\", \"right\""}}"#;
    let cli = FakeCli::printing(false, &envelope(invalid), "");
    let err = parse_op_plan_with(&cli, "{}").await.unwrap_err();
    assert_eq!(err.to_string(), "Invalid rotate action: \"dir\" must be one of \"left\", \"right\"");
}

/// Every code is shown as core's `message` says it, a code this server has never seen included;
/// a document without one falls back to core's detail in the same §1 form.
#[test]
fn a_refusal_is_cores_own_message_whatever_its_code() {
    let refused = |error: &str| try_plan(&format!(r#"{{"status":"invalid","error":{error}}}"#)).unwrap_err();
    for (code, message) in [
        ("E_PLAN", "Invalid plan: more than 8 variants"),
        ("E_JSON_LIMIT", "Invalid plan: the plan's JSON nests deeper than 64 levels"),
        ("E_LATER", "Invalid plan: a rule added after this server"),
    ] {
        let err = refused(&format!(r#"{{"code":"{code}","detail":"d","message":"{message}"}}"#));
        assert!(matches!(err, OpPlanError::Plan(_)), "{code}: {err}");
        assert_eq!(err.to_string(), message, "{code}");
    }
    let bare = refused(r#"{"code":"E_PLAN","detail":"\"actions\" must be an array"}"#);
    assert_eq!(bare.to_string(), "Invalid plan: \"actions\" must be an array");
}

#[test]
fn a_chat_only_turn_is_its_text_and_an_unknown_status_is_no_plan() {
    let plan = try_plan(r#"{"status":"chatOnly","reply":"Sure — cropping trims edges."}"#).unwrap();
    assert!(plan.chat_only && plan.actions.is_empty() && plan.warnings.is_empty());
    assert_eq!(plan.reply, "Sure — cropping trims edges.");

    let err = try_plan(r#"{"status":"pending"}"#).unwrap_err();
    assert!(matches!(err, OpPlanError::Checker(_)), "got: {err}");
    assert!(err.to_string().starts_with("cannot check the plan: "), "{err}");
}

#[tokio::test]
async fn a_cli_built_from_another_registry_is_refused() {
    let (bytes, _) = fingerprint();
    let stale = format!(
        r#"{{"version":1,"surface":"mcp","registryBytes":{},"registryFnv1a64":"0000000000000000","result":{ROTATED}}}"#,
        bytes + 1
    );
    let cli = FakeCli::printing(true, &stale, "");
    let err = parse_op_plan_with(&cli, "{}").await.unwrap_err();
    assert!(matches!(err, OpPlanError::Checker(_)), "got: {err}");
    let text = err.to_string();
    assert!(text.contains("different opRegistry.json") && text.contains("refused"), "{text}");

    let cli = FakeCli::printing(true, &stale, "");
    let warning = registry_skew(&cli).await.expect("a skew warning");
    assert!(warning.contains(&format!("{} bytes", bytes + 1)), "{warning}");
    assert_eq!(cli.call().input.as_deref(), Some(""), "the probe judges an empty reply");
}

#[tokio::test]
async fn a_matching_cli_raises_no_startup_warning() {
    let cli = FakeCli::printing(true, &envelope(r#"{"status":"chatOnly","reply":""}"#), "");
    assert_eq!(registry_skew(&cli).await, None);
}

#[tokio::test]
async fn a_cli_that_cannot_judge_the_reply_is_a_checker_error() {
    // Exit 2: nothing on stdout, the reason on stderr.
    let cli = FakeCli::printing(false, "", "error: that reply is too large: 9 MiB (max 8 MiB)\n");
    let err = parse_op_plan_with(&cli, "{}").await.unwrap_err();
    assert!(matches!(err, OpPlanError::Checker(_)), "got: {err}");
    assert!(err.to_string().contains("that reply is too large"), "{err}");

    // A clean exit with no document, and a document of another envelope version.
    let cli = FakeCli::printing(true, "", "");
    let err = parse_op_plan_with(&cli, "{}").await.unwrap_err();
    assert!(err.to_string().contains("printed no plan-check document"), "{err}");
    let cli = FakeCli::printing(true, &envelope(ROTATED).replacen("\"version\":1", "\"version\":2", 1), "");
    let err = parse_op_plan_with(&cli, "{}").await.unwrap_err();
    assert!(err.to_string().contains("version 2"), "{err}");
    assert!(registry_skew(&FakeCli::printing(false, "", "error: boom\n")).await.is_some());
}

/// The fingerprint is the CLI contract's: the embedded bytes' length and 16 lowercase hex digits.
#[test]
fn the_fingerprint_is_fnv1a_64_over_the_embedded_registry() {
    let (bytes, fnv) = fingerprint();
    assert_eq!(bytes as usize, stencil_mcp::registry::REGISTRY_JSON.len());
    assert_eq!(fnv.len(), 16);
    assert!(fnv.bytes().all(|b| b.is_ascii_hexdigit() && !b.is_ascii_uppercase()), "{fnv}");
    let raw = std::fs::read(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../common/config/llm/opRegistry.json"
    ))
    .expect("the canonical registry");
    let hash = raw.iter().fold(0xcbf2_9ce4_8422_2325_u64, |h, b| (h ^ u64::from(*b)).wrapping_mul(0x100_0000_01b3));
    assert_eq!((bytes as usize, fnv), (raw.len(), format!("{hash:016x}")), "the embed is the canonical file");
}
