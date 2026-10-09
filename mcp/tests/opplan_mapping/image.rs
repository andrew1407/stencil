//! Contract §2.1 multi-image ops: `image` and `save` typed from core's result, and the drops
//! core makes when one hides in a variant or a preview, in core's own words.


use serde_json::json;
use stencil_mcp::opplan::{to_edit_requests, Action, Dir, FilterMode, OpPlanError, Variant};

use crate::common::{plan_of, try_plan};

/// Core's sentences for these codes (`core/opplan/planWalker*.cpp`), as `--plan-check` prints them.
const VARIANT_DROPPED: &str = "Dropped variant 1 (\"saved\") — \"save\" is a top-level action only \
    (§2.1) — not allowed inside variants; the rest of the plan ran";
const PREVIEW_DROPPED: &str = "Dropped the preview for ask option 1 (\"A\") — \"image\" is a top-level \
    action only (§2.1) — not allowed inside variants or previews; the option is still offered";

#[test]
fn image_and_save_type_into_their_actions() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"image","index":2},{"op":"save","name":"portrait 1"},{"op":"save"},
            {"op":"save","path":"keep/Here"},{"op":"save","path":""}
        ]}"##,
    );
    assert_eq!(
        plan.actions,
        [
            Action::Image { index: 2 },
            Action::Save { name: Some("portrait 1".to_string()), path: None },
            Action::Save { name: None, path: None },
            Action::Save { name: None, path: Some("keep/Here".to_string()) },
            // Core trims a path; one that trims to "" is no destination at all.
            Action::Save { name: None, path: None },
        ]
    );
}

#[test]
fn a_variant_holding_a_top_level_op_is_dropped_and_the_rest_of_the_plan_runs() {
    let plan = plan_of(
        &json!({"reply":"three takes","actions":[{"op":"rotate","dir":"right","times":1}],"variants":[
            {"label":"sepia","actions":[{"op":"filter","mode":"sepia"}]}],
            "warnings":[{"code":"W_VARIANT_DROPPED","op":"save","index":1,"label":"saved",
                         "message":VARIANT_DROPPED}]})
        .to_string(),
    );
    assert_eq!(plan.actions, [Action::Rotate { dir: Dir::Right, times: 1 }]);
    assert_eq!(
        plan.variants,
        [Variant {
            label: "sepia".to_string(),
            actions: vec![Action::Filter { mode: FilterMode::Sepia, tint: None }],
        }]
    );
    assert_eq!(plan.warnings, [VARIANT_DROPPED]);
}

#[test]
fn a_dropped_preview_keeps_its_option_and_names_it() {
    let plan = plan_of(
        &json!({"reply":"x","ask":{"question":"Q","mode":"single","options":[{"label":"A"},{"label":"B"}]},
            "warnings":[{"code":"W_PREVIEW_DROPPED","op":"image","index":1,"label":"A",
                         "message":PREVIEW_DROPPED}]})
        .to_string(),
    );
    assert_eq!(plan.ask.unwrap().options.len(), 2, "the option keeps its place");
    assert_eq!(plan.warnings[0], PREVIEW_DROPPED);
}

#[test]
fn a_plan_that_was_only_a_bad_variant_still_replies() {
    let plan = plan_of(
        r##"{"reply":"Saved it for you.","warnings":[
            {"code":"W_VARIANT_DROPPED","op":"save","index":1,"label":"saved","message":"dropped"}]}"##,
    );
    assert_eq!(plan.reply, "Saved it for you.");
    assert!(plan.actions.is_empty() && plan.variants.is_empty());
    assert_eq!(plan.warnings.len(), 1, "got: {:?}", plan.warnings);
    // Nothing to run — the caller answers with the reply + the warning, not an error.
    let requests = to_edit_requests(&plan, Some("photo.jpg"), "out", &mut Vec::new()).unwrap();
    assert!(requests.is_empty());
}

#[test]
fn variant_strictness_reads_in_cores_words() {
    // Unknown op inside a variant: skipped with a warning, the variant survives.
    let plan = plan_of(
        r##"{"reply":"x","variants":[{"label":"v","actions":[{"op":"rotate","dir":"left","times":1}]}],
            "warnings":[{"code":"W_UNKNOWN_OP","op":"teleport","message":"Skipped unknown operation \"teleport\""}]}"##,
    );
    assert_eq!(plan.variants[0].actions.len(), 1);
    assert_eq!(plan.warnings, ["Skipped unknown operation \"teleport\""]);

    // A known op with bad params: the whole plan fails as an action error.
    let err = try_plan(
        r##"{"status":"invalid","error":{"code":"E_ACTION","op":"rotate","detail":"\"dir\" must be one of \"left\", \"right\"","message":"Invalid rotate action: \"dir\" must be one of \"left\", \"right\""}}"##,
    )
    .unwrap_err();
    assert!(matches!(err, OpPlanError::Action { ref op, .. } if op == "rotate"), "got: {err}");
    assert_eq!(err.to_string(), "Invalid rotate action: \"dir\" must be one of \"left\", \"right\"");

    // A §13 forbidden op: refused outright, never dropped.
    let err = try_plan(
        r##"{"status":"invalid","error":{"code":"E_FORBIDDEN","op":"llm","detail":"the \"llm\" op is never model-drivable","message":"Invalid plan: the \"llm\" op is never model-drivable"}}"##,
    )
    .unwrap_err();
    assert!(matches!(err, OpPlanError::Plan(_)), "got: {err}");
    assert_eq!(err.to_string(), "Invalid plan: the \"llm\" op is never model-drivable");
}
