//! Typing core's normalized §2 forms: formula clear/disable, page + blank cm dims, frame
//! index/indices, variant labels — and label sanitization. Their validation is core's.


use stencil_mcp::opplan::{sanitize_label, Action, Axis, FormulaOp, PageSize};

use crate::common::plan_of;

#[test]
fn formula_clear_and_disable_forms_type() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"formula","axis":"y","expr":""},
            {"op":"formula","axis":"x","expr":"   "},
            {"op":"formula","enabled":false},
            {"op":"formula","enabled":true},
            {"op":"formula","axis":"x","expr":"x*2"}
        ]}"##,
    );
    assert_eq!(
        plan.actions,
        vec![
            Action::Formula(FormulaOp::Clear { axis: Axis::Y }),
            Action::Formula(FormulaOp::Clear { axis: Axis::X }),
            Action::Formula(FormulaOp::Enable(false)),
            Action::Formula(FormulaOp::Enable(true)),
            Action::Formula(FormulaOp::Set { axis: Axis::X, expr: "x*2".into() }),
        ]
    );
}

#[test]
fn page_custom_cm_dims_type_as_cm() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"page","width":20,"height":30},
            {"op":"page","width":0.1,"height":500},
            {"op":"page","format":"a4"}
        ]}"##,
    );
    assert_eq!(
        plan.actions,
        vec![
            Action::Page { size: PageSize::Cm { width: 20.0, height: 30.0 } },
            Action::Page { size: PageSize::Cm { width: 0.1, height: 500.0 } },
            Action::Page { size: PageSize::Format("a4".into()) },
        ]
    );
}

#[test]
fn blank_cm_dims_type_beside_or_instead_of_a_format() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"blank","color":"white","width":10,"height":15},
            {"op":"blank","color":"red","format":"a4","width":10,"height":15}
        ]}"##,
    );
    assert_eq!(
        plan.actions[0],
        Action::Blank { color: "white".into(), format: None, dims_cm: Some((10.0, 15.0)) }
    );
    assert_eq!(
        plan.actions[1],
        Action::Blank { color: "red".into(), format: Some("a4".into()), dims_cm: Some((10.0, 15.0)) }
    );
}

#[test]
fn frame_index_and_indices_type_as_one_list() {
    let single = plan_of(r##"{"reply":"x","actions":[{"op":"frame","index":24}]}"##);
    assert_eq!(single.actions, vec![Action::Frame { indices: vec![24] }]);
    let multi = plan_of(r##"{"reply":"x","actions":[{"op":"frame","indices":[0,30,60]}]}"##);
    assert_eq!(multi.actions, vec![Action::Frame { indices: vec![0, 30, 60] }]);
}

#[test]
fn variants_type_with_missing_labels_left_empty() {
    // A null label stays empty here; the positional `variant-N` stem comes at mapping time.
    let plan = plan_of(
        r##"{"reply":"x","actions":[],"variants":[
            {"label":"Rotated","actions":[{"op":"rotate","dir":"right","times":1}]},
            {"label":null,"actions":[{"op":"filter","mode":"bw"}]}
        ]}"##,
    );
    assert_eq!(plan.variants.len(), 2);
    assert_eq!(plan.variants[0].label, "Rotated");
    assert_eq!(plan.variants[1].label, "");
}

#[test]
fn labels_sanitize_to_lowercase_dashed_stems() {
    assert_eq!(sanitize_label("Rotated & Tinted!"), "rotated-tinted");
    assert_eq!(sanitize_label("  B&W  "), "b-w");
    assert_eq!(sanitize_label("__weird__"), "weird");
    assert_eq!(sanitize_label("!!!"), "");
    assert_eq!(sanitize_label("crop 50%"), "crop-50");
}
