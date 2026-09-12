//! Mapping an op plan onto CLI runs (contract §2): action collapse, crop, rotation,
//! filters and layouts.

mod common;

use stencil_mcp::args::build_argv;
use stencil_mcp::opplan::to_edit_requests;

use common::plan_of;
// ── Mapping onto EditParams / argv ──

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
        argv.iter().any(|a| a == "x1=10% aspect=4:3"),
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
    assert!(!argv.iter().any(|a| a == "--layout-frame"));
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
