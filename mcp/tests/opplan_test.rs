//! Op-plan parsing/validation (contract §1–§3) and the mapping onto CLI runs — pure, no
//! CLI binary and no network needed.

use stencil_mcp::args::build_argv;
use stencil_mcp::opplan::{
    format_ask, loads_without_tracing, parse_op_plan, sanitize_label, to_edit_requests, Action,
    Axis, Dir, FilterMode, FormulaOp, OpPlan, OpPlanError, PageSize, Variant, MAX_ASK_OPTIONS,
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

// ── Mapping onto EditParams / argv ──

fn plan_of(text: &str) -> OpPlan {
    parse_op_plan(text).unwrap()
}

#[test]
fn base_actions_collapse_into_one_cli_run() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"crop","spec":{"x1":"10%","x2":"-10%"}},
            {"op":"rotate","dir":"right","times":1},
            {"op":"filter","mode":"sepia"}
        ]}"##,
    );
    let requests = to_edit_requests(
        &plan,
        Some("photo.jpg"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    assert_eq!(requests.len(), 1);
    assert_eq!(requests[0].label, None);
    let argv = build_argv(&requests[0].params, None).unwrap();
    let expected_out = std::path::Path::new("out").join("result.png");
    assert_eq!(
        argv,
        [
            "-i",
            "photo.jpg",
            "-c",
            "x1=10% x2=-10%",
            "-r",
            "1",
            "--filter",
            "sepia",
            expected_out.to_str().unwrap(),
        ]
    );
    assert!(requests[0].params.overwrite);
}

#[test]
fn crop_aspect_rides_the_collapsed_crop_string() {
    // The CLI's core cropSpec resolves the ratio — collapse only joins the token in.
    let plan = plan_of(
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"x1":"10%","aspect":"4:3"}}]}"##,
    );
    let requests = to_edit_requests(
        &plan,
        Some("photo.jpg"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    let argv = build_argv(&requests[0].params, None).unwrap();
    assert!(
        argv.contains(&"x1=10% aspect=4:3".to_string()),
        "argv missing the aspect token: {argv:?}"
    );
}

#[test]
fn rotations_sum_into_a_signed_quarter_count() {
    // right 1 + left 2 = net -1 quarter.
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"rotate","dir":"right","times":1},
            {"op":"rotate","dir":"left","times":2}
        ]}"##,
    );
    let requests = to_edit_requests(
        &plan,
        Some("a.png"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    assert_eq!(requests[0].params.rotate, Some(-1));

    // left 2 + left 2 = net 0 → no -r flag at all.
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"rotate","dir":"left","times":2},
            {"op":"rotate","dir":"left","times":2}
        ]}"##,
    );
    let requests = to_edit_requests(
        &plan,
        Some("a.png"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    assert_eq!(requests[0].params.rotate, None);
}

#[test]
fn custom_filter_maps_to_the_tint_value_and_none_clears() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[{"op":"filter","mode":"custom","tint":"#7c3aed"}]}"##,
    );
    let requests = to_edit_requests(
        &plan,
        Some("a.png"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    assert_eq!(requests[0].params.filter.as_deref(), Some("#7c3aed"));

    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"filter","mode":"sepia"},
            {"op":"filter","mode":"none"},
            {"op":"rotate","dir":"right"}
        ]}"##,
    );
    let requests = to_edit_requests(
        &plan,
        Some("a.png"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    assert_eq!(requests[0].params.filter, None);
}

#[test]
fn layout_lines_become_an_inline_layout() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"layout","lines":[{"points":[{"x":0,"y":0},{"x":8,"y":12}]}]},
            {"op":"layout","lines":[{"points":[{"x":1,"y":1}],"color":"#ff0000"}]}
        ]}"##,
    );
    let requests = to_edit_requests(
        &plan,
        Some("a.png"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    match &requests[0].params.layout {
        Some(stencil_mcp::args::LayoutArg::Inline(layout)) => {
            assert_eq!(layout.lines.len(), 2); // two layout actions concatenate
            assert_eq!(layout.lines[1].color.as_deref(), Some("#ff0000"));
        }
        other => panic!("expected an inline layout, got {other:?}"),
    }
    // Plan coordinates are snapshot-frame → the run passes `--layout-frame source`.
    assert_eq!(requests[0].params.layout_frame.as_deref(), Some("source"));
}

#[test]
fn layout_frame_source_rides_plan_layouts_and_current_omits_it() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"crop","spec":{"x1":"100px","y1":"0px"}},
            {"op":"layout","lines":[{"points":[{"x":150,"y":50}]}]}
        ],"variants":[
            {"label":"Marked","actions":[{"op":"layout","lines":[{"points":[{"x":1,"y":1}]}]}]}
        ]}"##,
    );
    // Plan execution: base and variant runs both draw snapshot-frame lines.
    let requests = to_edit_requests(
        &plan,
        Some("a.png"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    assert_eq!(requests[0].params.layout_frame.as_deref(), Some("source"));
    assert_eq!(requests[1].params.layout_frame.as_deref(), Some("source"));
    let argv = build_argv(&requests[0].params, Some("lay.json")).unwrap();
    let at = argv.iter().position(|a| a == "--layout-frame").unwrap();
    assert_eq!(argv[at + 1], "source");
    assert_eq!(argv[at - 2], "-l"); // rides right after the layout it qualifies

    // No layout in the run → no flag either.
    let plan = plan_of(r##"{"reply":"x","actions":[{"op":"rotate","dir":"right"}]}"##);
    let requests = to_edit_requests(
        &plan,
        Some("a.png"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    assert_eq!(requests[0].params.layout_frame, None);
    let argv = build_argv(&requests[0].params, None).unwrap();
    assert!(!argv.contains(&"--layout-frame".to_string()));
}

#[test]
fn blank_and_page_map_to_blank_params_without_input() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[{"op":"page","format":"b5"},{"op":"blank","color":"red"}]}"##,
    );
    let requests = to_edit_requests(
        &plan,
        Some("ignored.png"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    let argv = build_argv(&requests[0].params, None).unwrap();
    let expected_out = std::path::Path::new("out").join("result.png");
    assert_eq!(argv, ["--blank", "b5", "red", expected_out.to_str().unwrap()]);
}

#[test]
fn blank_own_format_wins_over_a_page_action() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[{"op":"page","format":"a5"},{"op":"blank","color":"red","format":"c7"}]}"##,
    );
    let requests = to_edit_requests(
        &plan,
        None,
        "out",
        &mut Vec::new(),
    ).unwrap();
    let argv = build_argv(&requests[0].params, None).unwrap();
    assert_eq!(argv[1], "c7");
}

#[test]
fn page_custom_cm_dims_map_to_blank_pixel_dims() {
    // 20cm / 30cm at the core's 96 dpi (cm / 2.54 * 96, half-up) = 756 x 1134 px.
    let plan = plan_of(
        r##"{"reply":"x","actions":[{"op":"page","width":20,"height":30},{"op":"blank","color":"white"}]}"##,
    );
    let requests = to_edit_requests(&plan, None, "out", &mut Vec::new()).unwrap();
    let argv = build_argv(&requests[0].params, None).unwrap();
    let expected_out = std::path::Path::new("out").join("result.png");
    assert_eq!(argv, ["--blank", "756", "1134", "white", expected_out.to_str().unwrap()]);
}

#[test]
fn blank_cm_dims_override_its_format_and_a_page_action() {
    // 10cm / 15cm → 378 x 567 px; the explicit dims beat both formats.
    let plan = plan_of(
        r##"{"reply":"x","actions":[{"op":"page","format":"a5"},
            {"op":"blank","color":"white","format":"a4","width":10,"height":15}]}"##,
    );
    let requests = to_edit_requests(&plan, None, "out", &mut Vec::new()).unwrap();
    let argv = build_argv(&requests[0].params, None).unwrap();
    assert_eq!(&argv[..4], ["--blank", "378", "567", "white"]);
}

#[test]
fn formula_clear_and_disable_are_skipped_with_one_note_not_an_error() {
    // A clear-only plan runs nothing: accepted, noted, zero requests.
    let plan = plan_of(
        r##"{"reply":"x","actions":[{"op":"formula","axis":"y","expr":""},{"op":"formula","enabled":false}]}"##,
    );
    let mut notes = Vec::new();
    let requests = to_edit_requests(&plan, Some("a.png"), "out", &mut notes).unwrap();
    assert!(requests.is_empty(), "an inert clear must not spawn a CLI run");
    assert_eq!(notes.iter().filter(|n| n.contains("formula")).count(), 1, "{notes:?}");

    // Riding beside real work (top-level or in a variant): the work still runs.
    let plan = plan_of(
        r##"{"reply":"x","actions":[{"op":"formula","enabled":false},{"op":"rotate","dir":"right"}],
            "variants":[{"label":"v","actions":[{"op":"formula","axis":"x","expr":""},{"op":"filter","mode":"bw"}]}]}"##,
    );
    let mut notes = Vec::new();
    let requests = to_edit_requests(&plan, Some("a.png"), "out", &mut notes).unwrap();
    assert_eq!(requests.len(), 2);
    assert_eq!(requests[0].params.rotate, Some(1));
    assert_eq!(requests[1].params.filter.as_deref(), Some("bw"));
    assert_eq!(notes.iter().filter(|n| n.contains("formula")).count(), 1, "{notes:?}");
}

#[test]
fn frame_maps_to_the_frame_index() {
    let plan = plan_of(r##"{"reply":"x","actions":[{"op":"frame","index":24}]}"##);
    let requests = to_edit_requests(
        &plan,
        Some("clip.mp4"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    assert_eq!(requests[0].params.frame, Some(24));
}

#[test]
fn variants_branch_from_the_base_actions() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[{"op":"crop","spec":{"y2":"50%"}}],"variants":[
            {"label":"Rotated","actions":[{"op":"rotate","dir":"right"}]},
            {"label":"B&W","actions":[{"op":"filter","mode":"bw"}]}
        ]}"##,
    );
    let requests = to_edit_requests(
        &plan,
        Some("photo.jpg"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    assert_eq!(requests.len(), 3); // base + 2 variants
    assert_eq!(requests[0].label, None);
    assert_eq!(requests[1].label.as_deref(), Some("rotated"));
    assert_eq!(requests[2].label.as_deref(), Some("b-w"));

    // Each variant replays the base crop before its own action.
    let argv = build_argv(&requests[1].params, None).unwrap();
    let crop_at = argv.iter().position(|a| a == "-c").unwrap();
    assert_eq!(argv[crop_at + 1], "y2=50%");
    assert!(argv.contains(&"-r".to_string()));
    let expected = std::path::Path::new("out").join("rotated.png");
    assert_eq!(argv.last().unwrap(), expected.to_str().unwrap());
}

#[test]
fn empty_actions_with_variants_write_no_base_result() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[],"variants":[
            {"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]}
        ]}"##,
    );
    let requests = to_edit_requests(
        &plan,
        Some("photo.jpg"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    assert_eq!(requests.len(), 1);
    assert_eq!(requests[0].label.as_deref(), Some("sepia"));
}

#[test]
fn colliding_and_empty_variant_labels_get_distinct_names() {
    let plan = OpPlan {
        reply: "x".into(),
        actions: Vec::new(),
        variants: vec![
            Variant {
                label: "Same!".into(),
                actions: vec![Action::Rotate {
                    dir: Dir::Right,
                    times: 1,
                }],
            },
            Variant {
                label: "same".into(),
                actions: vec![Action::Rotate {
                    dir: Dir::Left,
                    times: 1,
                }],
            },
            Variant {
                label: "!!!".into(),
                actions: vec![Action::Filter {
                    mode: FilterMode::Bw,
                    tint: None,
                }],
            },
        ],
        warnings: Vec::new(),
        chat_only: false,
        ask: None,
    };
    let requests = to_edit_requests(
        &plan,
        Some("a.png"),
        "out",
        &mut Vec::new(),
    ).unwrap();
    let labels: Vec<_> = requests.iter().map(|r| r.label.as_deref().unwrap()).collect();
    assert_eq!(labels, ["same", "same-2", "variant-3"]);
}

#[test]
fn unmappable_plans_are_rejected_with_clear_messages() {
    // formula: the CLI has no formula flag.
    let plan = plan_of(r##"{"reply":"x","actions":[{"op":"formula","axis":"x","expr":"x*2"}]}"##);
    let err = to_edit_requests(
        &plan,
        Some("a.png"),
        "out",
        &mut Vec::new(),
    ).unwrap_err();
    assert!(err.to_string().contains("formula"), "got: {err}");

    // page without a blank.
    let plan = plan_of(r##"{"reply":"x","actions":[{"op":"page","format":"a4"}]}"##);
    let err = to_edit_requests(
        &plan,
        Some("a.png"),
        "out",
        &mut Vec::new(),
    ).unwrap_err();
    assert!(err.to_string().contains("blank"), "got: {err}");

    // two crops in one run.
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"crop","spec":{"x1":"10%"}},
            {"op":"crop","spec":{"x2":"-10%"}}
        ]}"##,
    );
    let err = to_edit_requests(
        &plan,
        Some("a.png"),
        "out",
        &mut Vec::new(),
    ).unwrap_err();
    assert!(err.to_string().contains("crop"), "got: {err}");

    // multiple frame indices in one run.
    let plan = plan_of(r##"{"reply":"x","actions":[{"op":"frame","indices":[0,30]}]}"##);
    let err = to_edit_requests(
        &plan,
        Some("clip.mp4"),
        "out",
        &mut Vec::new(),
    ).unwrap_err();
    assert!(err.to_string().contains("frame"), "got: {err}");

    // an editing plan with no input at all.
    let plan = plan_of(r##"{"reply":"x","actions":[{"op":"rotate","dir":"right"}]}"##);
    let err = to_edit_requests(
        &plan,
        None,
        "out",
        &mut Vec::new(),
    ).unwrap_err();
    assert!(err.to_string().contains("input"), "got: {err}");
}

// ── §2.1 multi-image ops: `image` switches to a turn attachment, `save` persists ──
// `stencil_prompt` carries a single `input`, so index 1 IS the working image and any
// higher index is an attachment this turn cannot satisfy: a per-action note, never a
// failed plan. A `save` writes `{output_dir}/{name}.stencil` through the CLI's own
// project bundling.

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

#[test]
fn a_save_writes_a_stencil_project_and_image_restarts_the_working_image() {
    // crop → save → back to the input → rotate: the project bundles the CROP, and the base
    // result carries only the actions after the switch.
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"crop","spec":{"x1":"10%"}},
            {"op":"save","name":"Portrait 1"},
            {"op":"image","index":1},
            {"op":"rotate","dir":"right"}
        ]}"##,
    );
    let mut notes = Vec::new();
    let requests =
        to_edit_requests(&plan, Some("photo.jpg"), "out", &mut notes).unwrap();
    assert!(notes.is_empty(), "index 1 is the tool's own input: {notes:?}");
    assert_eq!(requests.len(), 2);

    // The base result: the post-switch actions only, in the fresh image's frame.
    assert_eq!(requests[0].label, None);
    assert!(!requests[0].project);
    assert_eq!(requests[0].params.rotate, Some(1));
    assert!(requests[0].params.crop.is_none(), "the crop belonged to the saved image");

    // The save: a `.stencil` project holding the crop that preceded it.
    assert_eq!(requests[1].label.as_deref(), Some("portrait-1"));
    assert!(requests[1].project);
    assert_eq!(
        requests[1].params.output,
        std::path::Path::new("out")
            .join("portrait-1.stencil")
            .to_string_lossy()
    );
    let argv = build_argv(&requests[1].params, None).unwrap();
    assert!(argv.contains(&"x1=10%".to_string()), "got: {argv:?}");
    assert_eq!(argv.last().unwrap(), &requests[1].params.output);
}

#[test]
fn an_unnamed_save_derives_its_name_from_the_input_file() {
    let plan = plan_of(r##"{"reply":"x","actions":[{"op":"rotate","dir":"right"},{"op":"save"}]}"##);
    let requests = to_edit_requests(
        &plan,
        Some("/photos/Cat Portrait.JPG"),
        "out",
        &mut Vec::new(),
    )
    .unwrap();
    assert_eq!(requests[1].label.as_deref(), Some("cat-portrait"));

    // Two saves under one derived name stay distinct rather than overwriting each other.
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"save"},{"op":"rotate","dir":"right"},{"op":"save"}]}"##,
    );
    let requests =
        to_edit_requests(
            &plan,
            Some("a.png"),
            "out",
                &mut Vec::new(),
        ).unwrap();
    let names: Vec<&str> = requests
        .iter()
        .filter(|r| r.project)
        .map(|r| r.label.as_deref().unwrap())
        .collect();
    assert_eq!(names, ["a", "a-2"]);
}

#[test]
fn a_save_path_is_honored_inside_output_dir_as_a_folder_or_a_stencil_file() {
    // A relative folder: the derived stem lands inside it.
    let plan = plan_of(
        r##"{"reply":"x","actions":[{"op":"rotate","dir":"right"},
            {"op":"save","name":"Portrait","path":"keepers/best"}]}"##,
    );
    let mut notes = Vec::new();
    let requests = to_edit_requests(&plan, Some("a.png"), "out", &mut notes).unwrap();
    assert!(notes.is_empty(), "got: {notes:?}");
    assert_eq!(
        requests[1].params.output,
        std::path::Path::new("out")
            .join("keepers/best")
            .join("portrait.stencil")
            .to_string_lossy()
    );

    // A relative `.stencil` file name is the destination itself.
    let plan = plan_of(
        r##"{"reply":"x","actions":[{"op":"rotate","dir":"right"},
            {"op":"save","path":"keepers/My Cat.stencil"}]}"##,
    );
    let requests = to_edit_requests(&plan, Some("a.png"), "out", &mut Vec::new()).unwrap();
    assert!(requests[1].project);
    assert_eq!(
        requests[1].params.output,
        std::path::Path::new("out")
            .join("keepers/My Cat.stencil")
            .to_string_lossy()
    );
}

#[test]
fn a_save_path_that_would_escape_output_dir_saves_to_the_usual_place_with_a_note() {
    for escape in ["/tmp/out", "../up", "a/../../up", "~/Downloads"] {
        let text = format!(
            r##"{{"reply":"x","actions":[{{"op":"rotate","dir":"right"}},
                {{"op":"save","name":"p","path":"{escape}"}}]}}"##
        );
        let plan = plan_of(&text);
        let mut notes = Vec::new();
        let requests = to_edit_requests(&plan, Some("a.png"), "out", &mut notes).unwrap();
        assert!(
            notes.iter().any(|n| n.contains("Saved to the usual place") && n.contains(escape)),
            "{escape}: {notes:?}"
        );
        assert_eq!(
            requests[1].params.output,
            std::path::Path::new("out").join("p.stencil").to_string_lossy(),
            "{escape} fell back to the usual place"
        );
    }
}

#[test]
fn an_image_index_this_turn_cannot_satisfy_costs_that_action_not_the_plan() {
    let plan = plan_of(
        r##"{"reply":"x","actions":[
            {"op":"image","index":3},{"op":"filter","mode":"sepia"}]}"##,
    );
    let mut notes = Vec::new();
    let requests =
        to_edit_requests(&plan, Some("a.png"), "out", &mut notes).unwrap();
    assert!(
        notes.iter().any(|n| n.contains("attached image 3") && n.contains("single `input`")),
        "got: {notes:?}"
    );
    // The rest of the plan still ran, on the image the tool does have.
    assert_eq!(requests.len(), 1);
    assert_eq!(requests[0].params.filter.as_deref(), Some("sepia"));
}

#[test]
fn a_save_with_no_working_image_is_skipped_with_a_note() {
    let plan = plan_of(r##"{"reply":"x","actions":[{"op":"save","name":"nothing"}]}"##);
    let mut notes = Vec::new();
    let requests = to_edit_requests(&plan, None, "out", &mut notes).unwrap();
    assert!(requests.is_empty());
    assert!(
        notes.iter().any(|n| n.contains("no working image")),
        "got: {notes:?}"
    );
}

// ── §11 interactive replies (`ask`) ──
// This server is a TOOL, not a chat: the card is validated exactly like every other client,
// then surfaced to the CALLING agent as text + structured data. Previews are never shown.

fn ask_plan(ask: &str) -> OpPlan {
    parse_op_plan(&format!(r#"{{"version":1,"reply":"pick","ask":{ask}}}"#)).unwrap()
}

fn ask_rejects(ask: &str) {
    let text = format!(r#"{{"version":1,"reply":"pick","ask":{ask}}}"#);
    assert!(
        parse_op_plan(&text).is_err(),
        "expected this card to reject the plan: {ask}"
    );
}

#[test]
fn ask_card_parses_with_its_defaults() {
    let plan = ask_plan(r#"{"question":"Which tint?","options":[{"label":"Sepia"},{"label":"B&W"}]}"#);
    let card = plan.ask.expect("card");
    assert_eq!(card.question, "Which tint?");
    assert!(!card.multi);
    assert!(!card.allow_custom);
    assert_eq!(
        card.options.iter().map(|o| o.label.as_str()).collect::<Vec<_>>(),
        vec!["Sepia", "B&W"]
    );
}

#[test]
fn ask_multi_custom_row_and_trimming() {
    let plan = ask_plan(
        r#"{"question":"  Which?  ","mode":"multi","allowCustom":true,"customLabel":" Other ","options":[{"label":"  A  "},{"label":"B"}]}"#,
    );
    let card = plan.ask.expect("card");
    assert!(card.multi);
    assert!(card.allow_custom);
    assert_eq!(card.question, "Which?");
    assert_eq!(card.custom_label, "Other");
    assert_eq!(card.options[0].label, "A");
}

#[test]
fn no_card_on_an_ordinary_or_chat_only_turn() {
    assert!(plan_of(r#"{"version":1,"reply":"hi","actions":[]}"#).ask.is_none());
    assert!(plan_of("just chatting").ask.is_none());
}

#[test]
fn previews_are_dropped_with_one_note_never_the_option() {
    let plan = ask_plan(
        r#"{"question":"Which?","options":[{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]},{"label":"Web","image":{"url":"https://e/x.png"}}]}"#,
    );
    let card = plan.ask.expect("card");
    assert_eq!(card.options.len(), 2, "both options survive");
    let notes = plan
        .warnings
        .iter()
        .filter(|w| w.contains("option previews"))
        .count();
    assert_eq!(notes, 1, "one note per CARD, not per option");
}

#[test]
fn ask_option_action_previews_and_all_image_reference_forms_are_accepted() {
    // §11.1: options may carry `actions` (a render spec) or `image` — one of
    // url / projectId / scanIndex. This server shows no pictures, so every form
    // is accepted and only the label survives (§11.4) — never a rejection.
    let plan = ask_plan(
        r#"{"question":"Which?","options":[
            {"label":"Crop","actions":[{"op":"crop","spec":{"x1":"10%"}}]},
            {"label":"Web","image":{"url":"https://e/x.png"}},
            {"label":"Draft","image":{"projectId":"p_12"}},
            {"label":"Scan","image":{"scanIndex":3}}
        ]}"#,
    );
    let card = plan.ask.expect("card");
    assert_eq!(
        card.options.iter().map(|o| o.label.as_str()).collect::<Vec<_>>(),
        ["Crop", "Web", "Draft", "Scan"]
    );
}

#[test]
fn malformed_cards_reject_the_whole_plan() {
    ask_rejects(r#""hello""#);
    ask_rejects(r#"{"options":[{"label":"A"},{"label":"B"}]}"#);
    ask_rejects(r#"{"question":"   ","options":[{"label":"A"},{"label":"B"}]}"#);
    ask_rejects(r#"{"question":"Q","options":[]}"#);
    ask_rejects(r#"{"question":"Q","options":[{"label":"only"}]}"#);
    ask_rejects(
        r#"{"question":"Q","options":[{"label":"1"},{"label":"2"},{"label":"3"},{"label":"4"},{"label":"5"},{"label":"6"}]}"#,
    );
    ask_rejects(r#"{"question":"Q","mode":"maybe","options":[{"label":"A"},{"label":"B"}]}"#);
    ask_rejects(r#"{"question":"Q","options":[{"label":"A"},{"label":"B"}],"sneaky":1}"#);
    ask_rejects(r#"{"question":"Q","options":[{"label":"A","sneaky":1},{"label":"B"}]}"#);
    ask_rejects(r#"{"question":"Q","options":[{"label":""},{"label":"B"}]}"#);
    ask_rejects(
        r#"{"question":"Q","options":[{"label":"A","actions":[],"image":{"url":"https://e/x"}},{"label":"B"}]}"#,
    );
    ask_rejects(r#"{"question":"Q","options":[{"label":"A"},{"label":"B"}],"allowCustom":"yes"}"#);
}

#[test]
fn five_options_is_the_cap_and_it_parses() {
    let plan = ask_plan(
        r#"{"question":"Q","options":[{"label":"1"},{"label":"2"},{"label":"3"},{"label":"4"},{"label":"5"}]}"#,
    );
    assert_eq!(plan.ask.expect("card").options.len(), MAX_ASK_OPTIONS);
}

#[test]
fn a_card_is_formatted_as_a_numbered_list_for_the_calling_agent() {
    let plan = ask_plan(
        r#"{"question":"Which tint?","allowCustom":true,"options":[{"label":"Sepia"},{"label":"B&W"}]}"#,
    );
    let text = format_ask(&plan.ask.expect("card"));
    assert!(text.contains("Which tint?"), "got: {text}");
    assert!(text.contains("1. Sepia"), "got: {text}");
    assert!(text.contains("2. B&W"), "got: {text}");
    assert!(text.contains("answer freely"), "got: {text}");
    assert!(text.contains("next prompt"), "the agent is told how to answer: {text}");
}

#[test]
fn a_plan_may_both_edit_and_ask() {
    let plan = parse_op_plan(
        r#"{"version":1,"reply":"cropped; now pick","actions":[{"op":"rotate","dir":"left"}],"ask":{"question":"Which tint?","options":[{"label":"Sepia"},{"label":"B&W"}]}}"#,
    )
    .unwrap();
    assert_eq!(plan.actions.len(), 1);
    assert_eq!(plan.ask.expect("card").options.len(), 2);
}

// ── §7 auto-continuation ──

/// §7 auto-continuation: a plan whose actions load a picture (`blank`/`frame`) and drew
/// no layout LINE continues; drawn lines, variants, or an `ask` finish the turn.
#[test]
fn loads_without_tracing_follows_the_section7_rule() {
    let plan = |json: &str| parse_op_plan(json).unwrap();

    // A bare load continues; so does load + pixel-independent edits (crop/filter/page).
    assert!(loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"blank","color":"#ffffff"}]}"##
    )));
    assert!(loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"frame","index":0},{"op":"filter","mode":"bw"}]}"##
    )));

    // A drawn layout committed to its coordinates — but an EMPTY layout drew nothing.
    assert!(!loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"blank","color":"#ffffff"},
            {"op":"layout","lines":[{"points":[{"x":1,"y":2}]}]}]}"##
    )));
    assert!(loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"blank","color":"#ffffff"},
            {"op":"layout","lines":[]}]}"##
    )));

    // No load op ⇒ nothing new to look at; variants and an ask end the turn.
    assert!(!loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"filter","mode":"sepia"}]}"##
    )));
    assert!(!loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"blank","color":"#ffffff"}],
            "variants":[{"label":"v","actions":[{"op":"filter","mode":"bw"}]}]}"##
    )));
    assert!(!loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"blank","color":"#ffffff"}],
            "ask":{"question":"Which color?","options":[{"label":"Red"},{"label":"Blue"}]}}"##
    )));
}
