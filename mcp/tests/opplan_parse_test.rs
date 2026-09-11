//! Op-plan extraction tolerance and strict validation (contract §1) — pure, no CLI
//! binary and no network needed.

use stencil_mcp::opplan::{
    parse_op_plan, Action, Dir, OpPlanError,
};
// ── Extraction tolerance ──

#[test]
fn plain_text_is_a_chat_only_turn() {
    let plan = parse_op_plan("Sure! Cropping means trimming the edges.").unwrap();
    assert!(plan.chat_only);
    assert_eq!(plan.reply, "Sure! Cropping means trimming the edges.");
    assert!(plan.actions.is_empty() && plan.variants.is_empty() && plan.warnings.is_empty());
}

#[test]
fn braces_that_are_not_json_fall_back_to_chat_only() {
    let text = "set {x1} and {x2} to taste";
    let plan = parse_op_plan(text).unwrap();
    assert!(plan.chat_only);
    assert_eq!(plan.reply, text);
}

#[test]
fn fenced_json_parses() {
    let text = "Here you go:\n```json\n{\"version\":1,\"reply\":\"done\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"right\"}]}\n```";
    let plan = parse_op_plan(text).unwrap();
    assert!(!plan.chat_only);
    assert_eq!(plan.reply, "done");
    assert_eq!(
        plan.actions,
        vec![Action::Rotate {
            dir: Dir::Right,
            times: 1
        }]
    );
}

#[test]
fn first_balanced_object_wins_over_surrounding_prose() {
    let text = "I planned this: {\"reply\":\"ok\",\"actions\":[]} — anything else?";
    let plan = parse_op_plan(text).unwrap();
    assert!(!plan.chat_only);
    assert_eq!(plan.reply, "ok");
}

#[test]
fn nested_braces_and_strings_stay_balanced() {
    let text = r##"{"reply":"brace } in string","actions":[{"op":"crop","spec":{"x1":"10%"}}]}"##;
    let plan = parse_op_plan(text).unwrap();
    assert_eq!(plan.reply, "brace } in string");
    assert_eq!(
        plan.actions,
        vec![Action::Crop {
            x1: Some("10%".into()),
            x2: None,
            y1: None,
            y2: None,
            aspect: None
        }]
    );
}

#[test]
fn version_other_than_1_is_accepted_and_ignored() {
    let plan = parse_op_plan(r##"{"version":7,"reply":"ok"}"##).unwrap();
    assert_eq!(plan.reply, "ok");
    assert!(plan.actions.is_empty());
}

// ── Strict validation ──

#[test]
fn missing_or_empty_reply_is_tolerated_with_a_warning() {
    // §1 reply tolerance: a plan that carries work keeps running ("Done." + warning).
    let plan =
        parse_op_plan(r##"{"actions":[{"op":"rotate","dir":"left"}]}"##).unwrap();
    assert_eq!(plan.reply, "Done.");
    assert_eq!(plan.actions.len(), 1);
    assert!(plan.warnings.iter().any(|w| w.contains("omitted its reply")));

    // An EMPTY plan says so — a bare "Done." would read as a success that never occurred.
    for text in [
        r##"{"actions":[]}"##,
        r##"{"reply":""}"##,
        r##"{"reply":"   "}"##,
        r##"{"reply":42}"##,
    ] {
        let plan = parse_op_plan(text).unwrap();
        assert!(plan.reply.contains("empty plan"), "{text} → {}", plan.reply);
        assert!(
            !plan.warnings.iter().any(|w| w.contains("still ran")),
            "{text} → {:?}",
            plan.warnings
        );
    }
}

#[test]
fn unknown_op_is_skipped_with_a_warning() {
    let plan = parse_op_plan(
        r##"{"reply":"ok","actions":[{"op":"resize","width":100},{"op":"rotate","dir":"left","times":2}]}"##,
    )
    .unwrap();
    assert_eq!(plan.warnings, vec!["Skipped unknown operation \"resize\""]);
    assert_eq!(
        plan.actions,
        vec![Action::Rotate {
            dir: Dir::Left,
            times: 2
        }]
    );
}

#[test]
fn known_op_with_invalid_params_fails_the_whole_plan() {
    let cases = [
        // unknown field on a known op
        r##"{"reply":"x","actions":[{"op":"rotate","dir":"left","angle":45}]}"##,
        // rotate out of range
        r##"{"reply":"x","actions":[{"op":"rotate","dir":"left","times":4}]}"##,
        r##"{"reply":"x","actions":[{"op":"rotate","dir":"up"}]}"##,
        // filter traps
        r##"{"reply":"x","actions":[{"op":"filter","mode":"blur"}]}"##,
        r##"{"reply":"x","actions":[{"op":"filter","mode":"custom"}]}"##,
        r##"{"reply":"x","actions":[{"op":"filter","mode":"custom","tint":"#12"}]}"##,
        r##"{"reply":"x","actions":[{"op":"filter","mode":"bw","tint":"#112233"}]}"##,
        // crop traps
        r##"{"reply":"x","actions":[{"op":"crop","spec":{}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"left":"10%"}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"x1":"10q"}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"x1":10}}]}"##,
        // aspect traps — strict W:H, digits only, both positive
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":"0:3"}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":"4:0"}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":"-1:2"}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":"4:-3"}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":"3:4:5"}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":"a:b"}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":"1.5:2"}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":"4"}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":"4:"}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":":3"}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":"1e2:3"}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":""}}]}"##,
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"aspect":43}}]}"##,
        // formula traps
        r##"{"reply":"x","actions":[{"op":"formula","axis":"x","expr":"y*2"}]}"##,
        r##"{"reply":"x","actions":[{"op":"formula","axis":"z","expr":"x"}]}"##,
        // §2: `enabled` rides alone, and must be a boolean
        r##"{"reply":"x","actions":[{"op":"formula","enabled":false,"axis":"x"}]}"##,
        r##"{"reply":"x","actions":[{"op":"formula","enabled":false,"expr":"x*2"}]}"##,
        r##"{"reply":"x","actions":[{"op":"formula","enabled":"off"}]}"##,
        // page format must be a lowercase ISO name
        r##"{"reply":"x","actions":[{"op":"page","format":"A4"}]}"##,
        r##"{"reply":"x","actions":[{"op":"page","format":"letter"}]}"##,
        r##"{"reply":"x","actions":[{"op":"page","format":"a11"}]}"##,
        // page custom dims: exactly one form, both dims together, cm bounds 0.1..500
        r##"{"reply":"x","actions":[{"op":"page","format":"a4","width":20,"height":30}]}"##,
        r##"{"reply":"x","actions":[{"op":"page"}]}"##,
        r##"{"reply":"x","actions":[{"op":"page","width":20}]}"##,
        r##"{"reply":"x","actions":[{"op":"page","width":0.05,"height":30}]}"##,
        r##"{"reply":"x","actions":[{"op":"page","width":20,"height":501}]}"##,
        r##"{"reply":"x","actions":[{"op":"page","width":"20","height":30}]}"##,
        // blank traps
        r##"{"reply":"x","actions":[{"op":"blank","color":"#12"}]}"##,
        r##"{"reply":"x","actions":[{"op":"blank","color":"dark red"}]}"##,
        r##"{"reply":"x","actions":[{"op":"blank","color":"red","format":"B5"}]}"##,
        // blank custom dims ride together, cm bounds 0.1..500
        r##"{"reply":"x","actions":[{"op":"blank","color":"red","width":10}]}"##,
        r##"{"reply":"x","actions":[{"op":"blank","color":"red","width":10,"height":501}]}"##,
        r##"{"reply":"x","actions":[{"op":"blank","color":"red","width":0,"height":10}]}"##,
        // frame traps
        r##"{"reply":"x","actions":[{"op":"frame"}]}"##,
        r##"{"reply":"x","actions":[{"op":"frame","index":0,"indices":[1]}]}"##,
        r##"{"reply":"x","actions":[{"op":"frame","index":-1}]}"##,
        r##"{"reply":"x","actions":[{"op":"frame","indices":[]}]}"##,
        // layout traps
        r##"{"reply":"x","actions":[{"op":"layout","lines":[{"points":[{"x":0,"y":0}],"glow":true}]}]}"##,
        r##"{"reply":"x","actions":[{"op":"layout","lines":[{"points":[{"x":0}]}]}]}"##,
        r##"{"reply":"x","actions":[{"op":"layout","lines":[{"points":[{"x":0,"y":0}],"style":"wavy"}]}]}"##,
    ];
    for text in cases {
        let err = parse_op_plan(text).unwrap_err();
        assert!(
            matches!(err, OpPlanError::Action { .. }),
            "{text} → expected an action error, got: {err}"
        );
    }
}
