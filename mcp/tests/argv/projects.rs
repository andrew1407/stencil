//! Project-write parameters → argv (`cli/CONTRACT.md` §6.3–§6.5): one `--set-*` flag per field
//! given, the version guard, the file kinds as the CLI spells them, and what is refused unrun.

use serde_json::{json, Value};
use stencil_mcp::args::{
    build_project_file_argv, build_project_files_argv, build_project_update_argv, FileKind,
    ProjectFileParams, ProjectUpdateParams,
};

fn update(value: Value) -> Result<Vec<String>, String> {
    let params: ProjectUpdateParams = serde_json::from_value(value).expect("update params");
    let argv = build_project_update_argv("http://h:8090", &params).map_err(|e| e.to_string())?;
    Ok(argv.into_iter().map(|t| t.into_owned()).collect())
}

fn file(value: Value, output: &str) -> Vec<String> {
    let params: ProjectFileParams = serde_json::from_value(value).expect("file params");
    let argv = build_project_file_argv("http://h:8090", &params, output).unwrap();
    argv.into_iter().map(|t| t.into_owned()).collect()
}

#[test]
fn an_update_names_each_field_given_and_its_guard() {
    let argv = update(json!({ "id": "p_1_a", "name": "Plans v2", "keywords": ["floor", "draft"],
                              "color": "", "expires_at": 0, "if_version": 7 }));
    assert_eq!(
        argv.unwrap(),
        [
            "--server", "http://h:8090", "--project-update", "p_1_a", "--set-name", "Plans v2",
            "--set-keywords", "floor,draft", "--set-color", "", "--set-expires", "0",
            "--if-version", "7",
        ]
    );
    // [] clears the keywords; the other fields ride only when given.
    let cleared = update(json!({ "id": "p_1_a", "keywords": [], "blank_color": "#00ff00" }));
    let cleared = cleared.unwrap();
    assert_eq!(cleared[4..], ["--set-keywords", "", "--set-blank-color", "#00ff00"]);
}

#[test]
fn an_update_with_nothing_to_change_or_a_bad_field_is_refused_unrun() {
    let refused = |value: Value| update(value).unwrap_err();
    assert!(refused(json!({ "id": "p_1_a" })).contains("nothing to change"));
    assert!(refused(json!({ "id": "p_1_a", "if_version": 3 })).contains("nothing to change"));
    assert!(refused(json!({ "id": "p_1_a", "name": "  " })).contains("`name` must not be blank"));
    let comma = refused(json!({ "id": "p_1_a", "keywords": ["a,b"] }));
    assert!(comma.contains("without a comma"), "{comma}");
    let id = refused(json!({ "id": "../admin", "name": "x" }));
    assert_eq!(id, "`id` \"../admin\" is not a project id");
}

#[test]
fn a_download_names_its_kind_and_refuses_to_clobber_unless_asked() {
    let argv = file(json!({ "id": "p_1_a", "kind": "variant3", "output": "v.png" }), "/w/v.png");
    let head = ["--server", "http://h:8090", "--project-file", "p_1_a", "variant3"];
    assert_eq!(argv[..5], head);
    assert_eq!(argv[5..], ["--no-clobber", "/w/v.png"]);
    let over = json!({ "id": "p_1_a", "kind": "result", "output": "r.png", "overwrite": true });
    assert_eq!(file(over, "/w/r.png")[4..], ["result", "/w/r.png"]);

    let files = build_project_files_argv("http://h:8090", "p_1_a").unwrap();
    assert_eq!(files, ["--server", "http://h:8090", "--project-files", "p_1_a"]);
    assert!(build_project_files_argv("http://h:8090", "p 1").is_err());
}

/// Every kind the schema offers is the name the CLI's `--project-file` accepts.
#[test]
fn each_file_kind_is_spelled_as_the_server_names_it() {
    let names = ["original", "result", "video", "chat"]
        .into_iter()
        .map(String::from)
        .chain((1..=8).map(|n| format!("variant{n}")));
    for name in names {
        let kind: FileKind = serde_json::from_value(json!(name)).expect("a kind");
        assert_eq!(kind.as_str(), name);
    }
    assert!(serde_json::from_value::<FileKind>(json!("thumbnail")).is_err());
}
