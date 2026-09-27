//! `parse_op_plan` through the real CLI's `--plan-check`: the reply on stdin, core's verdict
//! typed back, and the CLI's registry the same as this server's. Self-skips without a binary.

mod common;
use common::e2e::cli_present;

use stencil_mcp::opplan::{self, Action, Dir, FilterMode, OpPlanError};
use stencil_mcp::pipeline::ProcessRunner;

#[tokio::test]
async fn a_fenced_plan_is_checked_by_core_and_typed() {
    if !cli_present() {
        return;
    }
    let reply = "Here:\n```json\n{\"reply\":\"ok\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\"}],\
                 \"variants\":[{\"label\":\"B&W\",\"actions\":[{\"op\":\"filter\",\"mode\":\"bw\"}]}]}\n```";
    let plan = opplan::parse_op_plan(reply).await.expect("a valid plan");
    assert_eq!(plan.reply, "ok");
    assert_eq!(plan.actions, [Action::Rotate { dir: Dir::Left, times: 1 }], "core applied the default");
    assert_eq!(plan.variants[0].label, "B&W");
    assert_eq!(plan.variants[0].actions, [Action::Filter { mode: FilterMode::Bw, tint: None }]);
}

#[tokio::test]
async fn prose_is_a_chat_only_turn_and_unknown_ops_are_notes() {
    if !cli_present() {
        return;
    }
    let plan = opplan::parse_op_plan("  Cropping trims the edges.  ").await.unwrap();
    assert!(plan.chat_only);
    assert_eq!(plan.reply, "Cropping trims the edges.");

    let plan = opplan::parse_op_plan(r#"{"actions":[{"op":"sharpen"},{"op":"rotate","dir":"right"}]}"#)
        .await
        .unwrap();
    assert_eq!(plan.reply, "Done.");
    assert_eq!(
        plan.warnings,
        ["Skipped unknown operation \"sharpen\"", "The model omitted its reply — the plan still ran"]
    );
}

#[tokio::test]
async fn an_invalid_or_forbidden_plan_is_refused_in_cores_words() {
    if !cli_present() {
        return;
    }
    let err = opplan::parse_op_plan(r#"{"reply":"x","actions":[{"op":"rotate","dir":"up"}]}"#)
        .await
        .unwrap_err();
    assert!(matches!(err, OpPlanError::Action { .. }), "got: {err}");
    assert_eq!(err.to_string(), "Invalid rotate action: \"dir\" must be one of \"left\", \"right\"");

    let err = opplan::parse_op_plan(r#"{"reply":"x","variants":[{"actions":[{"op":"llm"}]}]}"#)
        .await
        .unwrap_err();
    assert_eq!(err.to_string(), "Invalid plan: the \"llm\" op is never model-drivable");
}

#[tokio::test]
async fn the_built_cli_judges_by_this_servers_registry() {
    if !cli_present() {
        return;
    }
    assert_eq!(opplan::registry_skew(&ProcessRunner).await, None);
}
