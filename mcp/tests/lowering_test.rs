//! The §13 entry as ONE table: an op the prompt promises is an op this surface can run,
//! because the entry that carries its bullet carries the fold that executes it.

mod common;

use stencil_mcp::opplan::fold;
use stencil_mcp::opplan::{Action, Dir};
use stencil_mcp::registry::{descriptor, op_registry};

/// An op is promised and RUN off one entry: a known op's entry carries the fold that
/// executes it.
#[test]
fn every_registered_op_carries_its_own_lowering() {
    let mut fold = fold::Fold::default();
    for op in op_registry() {
        let descriptor = descriptor(op.name).unwrap_or_else(|| panic!("{} is active", op.name));
        // Handed an action it was NOT registered for, a fold does nothing and cannot fail —
        // the table is the only pairing that has to be right.
        let other = Action::Rotate {
            dir: Dir::Right,
            times: 0,
        };
        assert!(
            (descriptor.lower)(&other, &mut fold).is_ok(),
            "{}'s lowering must ignore an action of another op",
            op.name
        );
    }
}

/// The §2.1 `image`/`save` ops validate here but never ride a run — the plan splits at
/// them — so their lowering leaves the accumulating run untouched.
#[test]
fn the_multi_image_ops_lower_to_nothing() {
    let mut fold = fold::Fold::default();
    let save = Action::Save {
        name: Some("shot".into()),
        path: None,
    };
    (descriptor("save").expect("save is active").lower)(&save, &mut fold).unwrap();
    assert!(fold.crop.is_none() && fold.frame.is_none() && fold.lines.is_empty());
    assert_eq!(fold.quarters, 0);
}

/// §2/§3: a line's `pointColor` rides onto the run's layout, a later `layout` replaces the
/// lines an earlier one drew, and an empty `lines` clears them.
#[test]
fn the_last_layout_wins_with_its_point_colours_and_an_empty_one_clears() {
    let first = r##"{"points":[{"x":1,"y":2},{"x":3,"y":4}],"pointColor":"#ff0000"}"##;
    let second = r#"{"points":[{"x":5,"y":6},{"x":7,"y":8}]}"#;
    let plan = common::plan_of(&format!(
        r#"{{"reply":"","actions":[{{"op":"layout","lines":[{first}]}},{{"op":"layout","lines":[{second}]}},{{"op":"layout","lines":[]}}]}}"#
    ));
    let lower = descriptor("layout").expect("layout is active").lower;
    let mut fold = fold::Fold::default();
    lower(&plan.actions[0], &mut fold).unwrap();
    assert_eq!(fold.lines[0].point_color.as_deref(), Some("#ff0000"));
    lower(&plan.actions[1], &mut fold).unwrap();
    assert_eq!(fold.lines.len(), 1, "the second layout replaces the first");
    assert_eq!(fold.lines[0].points[0].x, 5.0);
    lower(&plan.actions[2], &mut fold).unwrap();
    assert!(fold.lines.is_empty(), "an empty lines array clears the lines");
}
