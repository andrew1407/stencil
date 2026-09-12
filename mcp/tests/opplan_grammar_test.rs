//! Op-plan param grammars (contract §2): crop specs and aspects, filters, layouts, blanks.

use stencil_mcp::opplan::{
    parse_op_plan, Action, FilterMode, OpPlanError,
};
#[test]
fn crop_tokens_accept_the_contract_grammar() {
    let plan = parse_op_plan(
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"x1":"10%","x2":"-10%","y1":"0","y2":"1.5cm"}}]}"##,
    )
    .unwrap();
    assert_eq!(
        plan.actions,
        vec![Action::Crop {
            x1: Some("10%".into()),
            x2: Some("-10%".into()),
            y1: Some("0".into()),
            y2: Some("1.5cm".into()),
            aspect: None
        }]
    );
}

#[test]
fn crop_aspect_key_accepted_alone_or_with_edges() {
    let plan = parse_op_plan(
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"x1":"10%","aspect":"4:3"}}]}"##,
    )
    .unwrap();
    assert_eq!(
        plan.actions,
        vec![Action::Crop {
            x1: Some("10%".into()),
            x2: None,
            y1: None,
            y2: None,
            aspect: Some("4:3".into())
        }]
    );
    // Aspect alone satisfies the at-least-one-key rule.
    let alone =
        parse_op_plan(r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":"16:9"}}]}"##)
            .unwrap();
    assert_eq!(
        alone.actions,
        vec![Action::Crop {
            x1: None,
            x2: None,
            y1: None,
            y2: None,
            aspect: Some("16:9".into())
        }]
    );
}

#[test]
fn crop_aspect_beside_the_spec_is_folded_in() {
    // §1 tolerance: models sometimes put "aspect" beside "spec" — same validation, folded.
    let beside = parse_op_plan(
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"x1":"10%"},"aspect":"3:4"}]}"##,
    )
    .unwrap();
    assert_eq!(
        beside.actions,
        vec![Action::Crop {
            x1: Some("10%".into()),
            x2: None,
            y1: None,
            y2: None,
            aspect: Some("3:4".into())
        }]
    );
    // Beside an EMPTY spec it still satisfies the at-least-one-key rule.
    let alone = parse_op_plan(
        r##"{"reply":"x","actions":[{"op":"crop","spec":{},"aspect":"1:1"}]}"##,
    )
    .unwrap();
    assert_eq!(
        alone.actions,
        vec![Action::Crop {
            x1: None,
            x2: None,
            y1: None,
            y2: None,
            aspect: Some("1:1".into())
        }]
    );
    // An IDENTICAL duplicate in both places is tolerated (the spec's value wins).
    let duplicate = parse_op_plan(
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":"4:3"},"aspect":"4:3"}]}"##,
    )
    .unwrap();
    assert_eq!(
        duplicate.actions,
        vec![Action::Crop {
            x1: None,
            x2: None,
            y1: None,
            y2: None,
            aspect: Some("4:3".into())
        }]
    );
    // A CONFLICTING duplicate is invalid params — the plan fails.
    let conflict = parse_op_plan(
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":"4:3"},"aspect":"3:4"}]}"##,
    )
    .unwrap_err();
    assert!(matches!(conflict, OpPlanError::Action { .. }), "got: {conflict}");
    // The beside spelling gets the same strict W:H validation.
    let bad = parse_op_plan(
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"x1":"10%"},"aspect":"1.5:2"}]}"##,
    )
    .unwrap_err();
    assert!(matches!(bad, OpPlanError::Action { .. }), "got: {bad}");
}

#[test]
fn valid_filter_layout_and_blank_parse() {
    let plan = parse_op_plan(
        r##"{"reply":"x","actions":[
            {"op":"filter","mode":"custom","tint":"#7c3aed"},
            {"op":"layout","lines":[{"points":[{"x":0,"y":0},{"x":8,"y":12}],
              "color":"#FF0000","thickness":3,"pointSize":0,"style":"dashed",
              "locked":false,"fillColor":"transparent"}]},
            {"op":"blank","color":"white","format":"b5"}
        ]}"##,
    )
    .unwrap();
    assert_eq!(plan.actions.len(), 3);
    assert_eq!(
        plan.actions[0],
        Action::Filter {
            mode: FilterMode::Custom,
            tint: Some("#7c3aed".into())
        }
    );
    match &plan.actions[1] {
        Action::Layout { lines } => {
            assert_eq!(lines.len(), 1);
            assert_eq!(lines[0].points.len(), 2);
            assert_eq!(lines[0].style.as_deref(), Some("dashed"));
        }
        other => panic!("expected a layout action, got {other:?}"),
    }
    assert_eq!(
        plan.actions[2],
        Action::Blank {
            color: "white".into(),
            format: Some("b5".into()),
            dims_cm: None
        }
    );
}
