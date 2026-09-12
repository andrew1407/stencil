//! Mapping an op plan onto CLI runs, continued: blanks and pages, formulas, frames,
//! variants, and the plans that cannot map at all.

mod common;

use stencil_mcp::args::build_argv;
use stencil_mcp::opplan::{
    to_edit_requests, Action, Dir, FilterMode, OpPlan, Variant,
};

use common::plan_of;
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
    assert!(argv.iter().any(|a| a == "-r"));
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
