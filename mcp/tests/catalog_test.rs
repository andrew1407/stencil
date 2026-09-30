//! What a client learns before it calls anything: each tool's annotations and output
//! schema, the resources embedded from the canonical files, and the prompt templates.

mod common;
use common::dispatch::Harness;

use rmcp::model::{GetPromptRequestParams, PaginatedRequestParams, ReadResourceRequestParams};
use rmcp::ServerHandler;
use serde_json::{json, Value};

async fn tools(h: &Harness) -> Vec<Value> {
    let listed = h.server.list_tools(Some(PaginatedRequestParams::default()), h.context()).await;
    let listed = listed.expect("the router lists its tools");
    listed.tools.iter().map(|t| serde_json::to_value(t).unwrap()).collect()
}

#[tokio::test]
async fn every_tool_publishes_an_object_output_schema_and_its_hints() {
    let h = Harness::new();
    let tools = tools(&h).await;
    for tool in &tools {
        assert_eq!(tool["outputSchema"]["type"], "object", "{}", tool["name"]);
    }
    let hints = |name: &str| {
        let tool = tools.iter().find(|t| t["name"] == name).expect("a listed tool");
        tool["annotations"].clone()
    };
    let read_only =
        ["stencil_probe", "stencil_script_check", "stencil_script_plan", "stencil_projects"];
    for name in read_only {
        assert_eq!(hints(name)["readOnlyHint"], true, "{name}");
    }
    let writes = ["stencil_edit", "stencil_script", "stencil_prompt", "stencil_script_emit"];
    for name in writes.into_iter().chain(["stencil_project_update", "stencil_project_file"]) {
        assert_eq!(hints(name)["destructiveHint"], true, "{name}");
    }
    // Every project tool reaches a server; the two that write are not read-only.
    for name in ["stencil_projects", "stencil_project_update", "stencil_project_file"] {
        assert_eq!(hints(name)["openWorldHint"], true, "{name}");
    }
    assert_eq!(hints("stencil_project_update")["readOnlyHint"], false);
    assert_eq!(hints("stencil_project_file")["readOnlyHint"], false);
    // A plan fetches a URL input whose block draws lines, and an `@layout` URL; a check never does.
    assert_eq!(hints("stencil_script_plan")["openWorldHint"], true);
    assert_eq!(hints("stencil_script_check")["openWorldHint"], false);
}

/// `layout_frame` is part of the edit schema now; the sandbox and the token never are.
#[tokio::test]
async fn the_edit_schema_exposes_layout_frame_and_hides_the_sandbox_and_the_token() {
    let h = Harness::new();
    let tools = tools(&h).await;
    let edit = tools.iter().find(|t| t["name"] == "stencil_edit").unwrap();
    let properties = edit["inputSchema"]["properties"].as_object().unwrap();
    assert!(properties.contains_key("layout_frame"));
    assert!(!properties.contains_key("confine_root") && !properties.contains_key("token"));
}

/// The opt-in `preview`: a boolean on the edit and script inputs, and an optional note of the
/// attached thumbnail in their output schemas — never a required field.
#[tokio::test]
async fn preview_is_an_optional_input_and_output_of_edit_and_script() {
    let tools = tools(&Harness::new()).await;
    for (name, out) in [("stencil_edit", "preview"), ("stencil_script", "previews")] {
        let tool = tools.iter().find(|t| t["name"] == name).unwrap();
        assert_eq!(tool["inputSchema"]["properties"]["preview"]["type"], "boolean", "{name}");
        assert!(tool["outputSchema"]["properties"].get(out).is_some(), "{name}");
        let required = tool["outputSchema"]["required"].as_array().unwrap();
        assert!(!required.contains(&json!(out)), "{name}: {out} is optional");
    }
}

#[tokio::test]
async fn the_resources_are_the_canonical_files() {
    let h = Harness::new();
    let listed = h.server.list_resources(None, h.context()).await.expect("resources");
    let uris: Vec<String> = listed.resources.iter().map(|r| r.raw.uri.clone()).collect();
    assert_eq!(uris.len(), 5, "{uris:?}");

    let read = |uri: &str| {
        let request = ReadResourceRequestParams::new(uri);
        h.server.read_resource(request, h.context())
    };
    let text_of = |result: rmcp::model::ReadResourceResult| {
        serde_json::to_value(&result.contents[0]).unwrap()["text"].as_str().unwrap().to_string()
    };
    let registry = text_of(read("stencil://config/opRegistry.json").await.unwrap());
    let canonical = include_str!("../../common/config/llm/opRegistry.json");
    assert_eq!(registry, canonical);

    let json_of = |text: String| serde_json::from_str::<Value>(&text).unwrap();
    let pages = json_of(text_of(read("stencil://config/page-formats.json").await.unwrap()));
    assert_eq!(pages["sizes"]["A4"], json!({ "width": 21, "height": 29.7 }));

    let filters = json_of(text_of(read("stencil://config/filters.json").await.unwrap()));
    assert!(filters["modes"].as_array().unwrap().contains(&json!("sepia")), "{filters}");

    assert!(read("stencil://config/nope.json").await.is_err());
}

#[tokio::test]
async fn a_prompt_fills_its_template_and_needs_its_required_arguments() {
    let h = Harness::new();
    let listed = h.server.list_prompts(None, h.context()).await.expect("prompts");
    let names: Vec<&str> = listed.prompts.iter().map(|p| p.name.as_str()).collect();
    assert_eq!(names, ["write_stc_script", "print_page_layout"]);

    let get = |name: &str, arguments: Value| {
        let request: GetPromptRequestParams =
            serde_json::from_value(json!({ "name": name, "arguments": arguments })).unwrap();
        h.server.get_prompt(request, h.context())
    };
    let page = get("print_page_layout", json!({ "content": "a 3x4 grid" })).await.unwrap();
    let text = serde_json::to_value(&page.messages[0]).unwrap()["content"]["text"].to_string();
    assert!(text.contains("a 3x4 grid on one page — format A4, portrait"), "{text}");
    assert!(!text.contains("{page}") && !text.contains("{content}"), "{text}");

    assert!(get("write_stc_script", json!({})).await.is_err(), "task is required");
}
