//! The §13 entry as ONE table: an op the validator knows is an op this surface can run,
//! because the entry that validated it carries the fold that executes it.

use stencil_mcp::opplan::fold;
use stencil_mcp::opplan::{Action, Dir};
use stencil_mcp::registry::{descriptor, op_registry};


/// An op is promised, validated and RUN off one entry: a known op's entry carries the fold
/// that executes it. Converges with pystencil's `OpSpec` (validator + applier).
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
