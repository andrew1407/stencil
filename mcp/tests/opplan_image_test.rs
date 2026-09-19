//! Contract §2.1 multi-image ops: `image` and `save` shapes, and their top-level-only rule.

mod common;

use stencil_mcp::opplan::{
    parse_op_plan, to_edit_requests, Action, Dir, FilterMode, OpPlanError, Variant,
};

use common::plan_of;
// ── §2.1 multi-image ops: `image` switches to a turn attachment, `save` persists ──
// `stencil_prompt` carries one `input`, so index 1 IS it and a higher index is a note.

#[test]
fn image_and_save_validate_their_shapes_and_are_top_level_only() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"image","index":2},{"op":"save","name":"portrait 1"},{"op":"save"}
        ]}"##,
    );
    assert_eq!(
        plan.actions,
        [
            Action::Image { index: 2 },
            Action::Save {
                name: Some("portrait 1".to_string()),
                path: None,
            },
            Action::Save { name: None, path: None },
        ]
    );

    // index is 1-based: 0, negatives and non-integers are not an attachment.
    for index in ["0", "-1", "1.5", "\"1\""] {
        let text = format!(r##"{{"reply":"x","actions":[{{"op":"image","index":{index}}}]}}"##);
        let err = parse_op_plan(&text).unwrap_err();
        assert!(err.to_string().contains("index"), "got: {err}");
    }
    let long = "x".repeat(121);
    let err =
        parse_op_plan(&format!(r##"{{"reply":"x","actions":[{{"op":"save","name":"{long}"}}]}}"##))
            .unwrap_err();
    assert!(err.to_string().contains("120"), "got: {err}");

    // §10 `path`: a string ≤ 1024 raw chars, trimmed, never a URL; "" ≡ absent.
    let plan = plan_of(r##"{"reply":"x","actions":[{"op":"save","path":"  keep/Here  "}]}"##);
    assert_eq!(
        plan.actions,
        [Action::Save { name: None, path: Some("keep/Here".to_string()) }]
    );
    let plan = plan_of(r##"{"reply":"x","actions":[{"op":"save","path":"   "}]}"##);
    assert_eq!(plan.actions, [Action::Save { name: None, path: None }]);
    let long = "p".repeat(1025);
    let err =
        parse_op_plan(&format!(r##"{{"reply":"x","actions":[{{"op":"save","path":"{long}"}}]}}"##))
            .unwrap_err();
    assert!(err.to_string().contains("1024"), "got: {err}");
    for bad in [r#""https://x.example/out.png""#, r#""file:///tmp/p.stencil""#, "7"] {
        let text = format!(r##"{{"reply":"x","actions":[{{"op":"save","path":{bad}}}]}}"##);
        let err = parse_op_plan(&text).unwrap_err();
        assert!(err.to_string().contains("path"), "got: {err}");
    }

    // …and neither may hide inside a variant or an ask-option preview: §1 drops the
    // variant / the preview with a warning, it never fails the plan.
    for op in [r#"{"op":"image","index":1}"#, r#"{"op":"save"}"#] {
        let plan = plan_of(&format!(
            r##"{{"reply":"x","variants":[{{"label":"v","actions":[{op}]}}]}}"##
        ));
        assert!(plan.variants.is_empty());
        assert!(
            plan.warnings.iter().any(|w| w.contains("variant 1 (\"v\")") && w.contains("top-level")),
            "got: {:?}",
            plan.warnings
        );

        let plan = plan_of(&format!(
            r##"{{"reply":"x","ask":{{"question":"Q","options":[
                {{"label":"A","actions":[{op}]}},{{"label":"B"}}]}}}}"##
        ));
        let card = plan.ask.unwrap();
        assert_eq!(card.options.len(), 2, "the option keeps its place, only the preview goes");
        assert!(
            plan.warnings
                .iter()
                .any(|w| w.contains("ask option 1 (\"A\")") && w.contains("top-level")),
            "got: {:?}",
            plan.warnings
        );
    }
}

#[test]
fn a_variant_holding_a_top_level_op_is_dropped_and_the_rest_of_the_plan_runs() {
    let plan = plan_of(
        r##"{"reply":"three takes","actions":[{"op":"rotate","dir":"right"}],"variants":[
            {"label":"saved","actions":[{"op":"filter","mode":"bw"},{"op":"save"}]},
            {"label":"sepia","actions":[{"op":"filter","mode":"sepia"}]}
        ]}"##,
    );
    // The top-level actions and the well-formed variant both survive.
    assert_eq!(plan.actions, [Action::Rotate { dir: Dir::Right, times: 1 }]);
    assert_eq!(
        plan.variants,
        [Variant {
            label: "sepia".to_string(),
            actions: vec![Action::Filter {
                mode: FilterMode::Sepia,
                tint: None
            }],
        }]
    );
    // …and the warning names the dropped one and why.
    assert_eq!(plan.warnings.len(), 1, "got: {:?}", plan.warnings);
    assert!(
        plan.warnings[0].contains("variant 1 (\"saved\")")
            && plan.warnings[0].contains("\"save\"")
            && plan.warnings[0].contains("top-level action only (§2.1)"),
        "got: {}",
        plan.warnings[0]
    );

    // An unlabelled variant is named by its 1-based position alone.
    let plan = plan_of(
        r##"{"reply":"x","variants":[{"actions":[{"op":"rotate","dir":"left"}]},
            {"actions":[{"op":"image","index":2}]}]}"##,
    );
    assert_eq!(plan.variants.len(), 1);
    assert!(
        plan.warnings[0].contains("variant 2:") && plan.warnings[0].contains("\"image\""),
        "got: {}",
        plan.warnings[0]
    );
}

#[test]
fn a_plan_that_was_only_a_bad_variant_still_replies() {
    let plan = parse_op_plan(
        r##"{"reply":"Saved it for you.","variants":[
            {"label":"saved","actions":[{"op":"save","name":"portrait"}]}]}"##,
    )
    .expect("a misplaced op in the only variant is not a plan error");
    assert_eq!(plan.reply, "Saved it for you.");
    assert!(plan.actions.is_empty() && plan.variants.is_empty());
    assert_eq!(plan.warnings.len(), 1, "got: {:?}", plan.warnings);
    assert!(plan.warnings[0].contains("variant 1 (\"saved\")"), "got: {}", plan.warnings[0]);
    // Nothing to run — the caller answers with the reply + the warning, not an error.
    let requests =
        to_edit_requests(&plan, Some("photo.jpg"), "out", &mut Vec::new())
            .unwrap();
    assert!(requests.is_empty());
}

#[test]
fn variant_strictness_is_otherwise_unchanged() {
    // Unknown op inside a variant: still skipped with a warning, the variant survives.
    let plan = plan_of(
        r##"{"reply":"x","variants":[{"label":"v","actions":[
            {"op":"teleport"},{"op":"rotate","dir":"left"}]}]}"##,
    );
    assert_eq!(plan.variants.len(), 1);
    assert_eq!(plan.variants[0].actions.len(), 1);
    assert!(plan.warnings[0].contains("Skipped unknown operation"), "got: {:?}", plan.warnings);

    // A known op with bad params inside a variant: still fails the whole plan.
    let err = parse_op_plan(
        r##"{"reply":"x","variants":[{"label":"v","actions":[{"op":"rotate","dir":"up"}]}]}"##,
    )
    .unwrap_err();
    assert!(matches!(err, OpPlanError::Action { .. }), "got: {err}");

    // A §13 forbidden op inside a variant: still refused outright, never dropped.
    let err = parse_op_plan(r##"{"reply":"x","variants":[{"actions":[{"op":"llm"}]}]}"##)
        .unwrap_err();
    assert!(err.to_string().contains("never model-drivable"), "got: {err}");
}
