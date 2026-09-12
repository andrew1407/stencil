//! Contract §2.1 execution: what a `save` writes and where, and what `image` restarts.

mod common;

use stencil_mcp::args::build_argv;
use stencil_mcp::opplan::to_edit_requests;

use common::plan_of;
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
    assert!(argv.iter().any(|a| a == "x1=10%"), "got: {argv:?}");
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
