//! Op-plan §2 widened forms, the §3 limits, variants, and label sanitization.

use stencil_mcp::opplan::{
    parse_op_plan, sanitize_label, Action,
    Axis, FormulaOp, OpPlanError, PageSize,
};
// ── §2 widened forms: formula clear/disable, page + blank custom cm dims ──

#[test]
fn formula_clear_and_disable_forms_parse() {
    let plan = parse_op_plan(
        r##"{"reply":"x","actions":[
            {"op":"formula","axis":"y","expr":""},
            {"op":"formula","axis":"x","expr":"   "},
            {"op":"formula","enabled":false},
            {"op":"formula","enabled":true},
            {"op":"formula","axis":"x","expr":"x*2"}
        ]}"##,
    )
    .unwrap();
    assert_eq!(
        plan.actions,
        vec![
            Action::Formula(FormulaOp::Clear { axis: Axis::Y }),
            Action::Formula(FormulaOp::Clear { axis: Axis::X }),
            Action::Formula(FormulaOp::Enable(false)),
            Action::Formula(FormulaOp::Enable(true)),
            Action::Formula(FormulaOp::Set {
                axis: Axis::X,
                expr: "x*2".into()
            }),
        ]
    );
}

#[test]
fn page_custom_cm_dims_parse_inclusive_of_the_bounds() {
    let plan = parse_op_plan(
        r##"{"reply":"x","actions":[
            {"op":"page","width":20,"height":30},
            {"op":"page","width":0.1,"height":500}
        ]}"##,
    )
    .unwrap();
    assert_eq!(
        plan.actions,
        vec![
            Action::Page {
                size: PageSize::Cm {
                    width: 20.0,
                    height: 30.0
                }
            },
            Action::Page {
                size: PageSize::Cm {
                    width: 0.1,
                    height: 500.0
                }
            },
        ]
    );
}

#[test]
fn blank_cm_dims_parse_beside_or_instead_of_a_format() {
    let plan = parse_op_plan(
        r##"{"reply":"x","actions":[
            {"op":"blank","color":"white","width":10,"height":15},
            {"op":"blank","color":"red","format":"a4","width":10,"height":15}
        ]}"##,
    )
    .unwrap();
    assert_eq!(
        plan.actions[0],
        Action::Blank {
            color: "white".into(),
            format: None,
            dims_cm: Some((10.0, 15.0))
        }
    );
    assert_eq!(
        plan.actions[1],
        Action::Blank {
            color: "red".into(),
            format: Some("a4".into()),
            dims_cm: Some((10.0, 15.0))
        }
    );
}

#[test]
fn frame_index_and_indices_normalize_to_one_list() {
    let single = parse_op_plan(r##"{"reply":"x","actions":[{"op":"frame","index":24}]}"##).unwrap();
    assert_eq!(single.actions, vec![Action::Frame { indices: vec![24] }]);
    let multi =
        parse_op_plan(r##"{"reply":"x","actions":[{"op":"frame","indices":[0,30,60]}]}"##).unwrap();
    assert_eq!(
        multi.actions,
        vec![Action::Frame {
            indices: vec![0, 30, 60]
        }]
    );
}

// ── Limits ──

#[test]
fn limits_are_enforced() {
    // > 16 actions
    let actions: Vec<String> = (0..17)
        .map(|_| r##"{"op":"rotate","dir":"left"}"##.to_string())
        .collect();
    let text = format!(r##"{{"reply":"x","actions":[{}]}}"##, actions.join(","));
    assert!(matches!(
        parse_op_plan(&text).unwrap_err(),
        OpPlanError::Plan(_)
    ));

    // > 8 variants
    let variants: Vec<String> = (0..9).map(|i| format!(r##"{{"label":"v{i}"}}"##)).collect();
    let text = format!(r##"{{"reply":"x","variants":[{}]}}"##, variants.join(","));
    assert!(matches!(
        parse_op_plan(&text).unwrap_err(),
        OpPlanError::Plan(_)
    ));

    // > 200 layout lines
    let lines: Vec<String> = (0..201)
        .map(|_| r##"{"points":[{"x":0,"y":0}]}"##.to_string())
        .collect();
    let text = format!(
        r##"{{"reply":"x","actions":[{{"op":"layout","lines":[{}]}}]}}"##,
        lines.join(",")
    );
    assert!(matches!(
        parse_op_plan(&text).unwrap_err(),
        OpPlanError::Action { .. }
    ));

    // > 5000 chars in a string field
    let text = format!(
        r##"{{"reply":"x","actions":[{{"op":"formula","axis":"x","expr":"{}"}}]}}"##,
        "x+".repeat(2501)
    );
    assert!(matches!(
        parse_op_plan(&text).unwrap_err(),
        OpPlanError::Action { .. }
    ));

    // 32 frame indices pass, 33 fail
    let ok = format!(
        r##"{{"reply":"x","actions":[{{"op":"frame","indices":[{}]}}]}}"##,
        (0..32).map(|i| i.to_string()).collect::<Vec<_>>().join(",")
    );
    assert!(parse_op_plan(&ok).is_ok());
    let bad = format!(
        r##"{{"reply":"x","actions":[{{"op":"frame","indices":[{}]}}]}}"##,
        (0..33).map(|i| i.to_string()).collect::<Vec<_>>().join(",")
    );
    assert!(parse_op_plan(&bad).is_err());
}

// ── Variants ──

#[test]
fn variants_parse_with_missing_labels_left_empty() {
    // A missing label stays empty at parse time; the positional `variant-N` fallback is
    // applied at mapping time (see the distinct-names test below).
    let plan = parse_op_plan(
        r##"{"reply":"x","actions":[],"variants":[
            {"label":"Rotated","actions":[{"op":"rotate","dir":"right"}]},
            {"actions":[{"op":"filter","mode":"bw"}]}
        ]}"##,
    )
    .unwrap();
    assert_eq!(plan.variants.len(), 2);
    assert_eq!(plan.variants[0].label, "Rotated");
    assert_eq!(plan.variants[1].label, "");
}

// ── Label sanitization ──

#[test]
fn labels_sanitize_to_lowercase_dashed_stems() {
    assert_eq!(sanitize_label("Rotated & Tinted!"), "rotated-tinted");
    assert_eq!(sanitize_label("  B&W  "), "b-w");
    assert_eq!(sanitize_label("__weird__"), "weird");
    assert_eq!(sanitize_label("!!!"), "");
    assert_eq!(sanitize_label("crop 50%"), "crop-50");
}
