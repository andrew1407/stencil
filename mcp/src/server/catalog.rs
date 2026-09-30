//! The resources and prompts this server offers. Every resource is a canonical file embedded
//! at compile time (or a table read out of one); their names, descriptions and the prompt
//! templates are prose, so they live in `toolDescriptions.json`.

use std::borrow::Cow;

use rmcp::model::{
    AnnotateAble, GetPromptResult, JsonObject, Prompt, PromptArgument, PromptMessage,
    PromptMessageRole, RawResource, ReadResourceResult, Resource, ResourceContents,
};
use rmcp::ErrorData as McpError;
use serde_json::{json, Value};

use super::PROSE;
use crate::registry::REGISTRY_JSON as OP_REGISTRY;

const STC_CONTRACT: &str = include_str!("../../../contracts/stc/stc-contract.md");
const CONSTANTS: &str = include_str!("../../../common/config/constants.json");
const COLOR_NAMES: &str = include_str!("../../../common/config/colorNames.json");

fn entries(key: &str) -> &'static [Value] {
    PROSE[key].as_array().map(Vec::as_slice).unwrap_or_default()
}

fn text<'a>(entry: &'a Value, key: &str) -> &'a str {
    entry[key].as_str().unwrap_or_default()
}

pub fn resources() -> Vec<Resource> {
    let listed = entries("resources").iter().map(|entry| {
        RawResource::new(text(entry, "uri"), text(entry, "name"))
            .with_description(text(entry, "description"))
            .with_mime_type(text(entry, "mimeType"))
            .no_annotation()
    });
    listed.collect()
}

pub fn read(uri: &str) -> Result<ReadResourceResult, McpError> {
    let entry = entries("resources").iter().find(|entry| text(entry, "uri") == uri);
    let (Some(entry), Some(body)) = (entry, content(uri)) else {
        return Err(McpError::resource_not_found(format!("no resource '{uri}'"), None));
    };
    let contents = ResourceContents::text(body, uri).with_mime_type(text(entry, "mimeType"));
    Ok(ReadResourceResult::new(vec![contents]))
}

/// The body behind each resource URI.
fn content(uri: &str) -> Option<Cow<'static, str>> {
    Some(match uri.strip_prefix("stencil://")? {
        "config/opRegistry.json" => Cow::Borrowed(OP_REGISTRY),
        "contracts/stc-contract.md" => Cow::Borrowed(STC_CONTRACT),
        "config/colors.json" => Cow::Borrowed(COLOR_NAMES),
        "config/page-formats.json" => Cow::Owned(page_formats()),
        "config/filters.json" => Cow::Owned(filters()),
        _ => return None,
    })
}

/// `PAGE_SIZES` from `constants.json`: portrait width × height in centimetres.
fn page_formats() -> String {
    let constants: Value = serde_json::from_str(CONSTANTS).unwrap_or_default();
    let sizes = &constants["PAGE_SIZES"];
    pretty(json!({ "unit": "cm", "orientation": "portrait", "dpi": 96, "sizes": sizes }))
}

/// The image-filter modes of the registry's `mcp` filter op; a colour instead tints.
fn filters() -> String {
    let registry: Value = serde_json::from_str(OP_REGISTRY).unwrap_or_default();
    let ops = registry["ops"].as_array().cloned().unwrap_or_default();
    let mcp = |op: &&Value| op["profiles"].as_array().is_some_and(|p| p.contains(&json!("mcp")));
    let filter = ops.iter().filter(|op| op["name"] == "filter").find(mcp);
    let modes: Vec<Value> = filter
        .and_then(|op| op["keys"]["mode"]["enum"].as_array().cloned())
        .unwrap_or_default()
        .into_iter()
        .filter(|mode| mode != "custom")
        .collect();
    pretty(json!({ "modes": modes, "tint": "any colour name or #hex from colors.json" }))
}

fn pretty(value: Value) -> String {
    serde_json::to_string_pretty(&value).unwrap_or_default()
}

pub fn prompts() -> Vec<Prompt> {
    let listed = entries("prompts").iter().map(|entry| {
        let arguments = entry["arguments"].as_array().map(|args| {
            let argument = |arg: &Value| {
                PromptArgument::new(text(arg, "name"))
                    .with_description(text(arg, "description"))
                    .with_required(arg["required"].as_bool().unwrap_or(false))
            };
            args.iter().map(argument).collect()
        });
        Prompt::new(text(entry, "name"), Some(text(entry, "description")), arguments)
            .with_title(text(entry, "title"))
    });
    listed.collect()
}

/// The prompt's template with every `{argument}` filled: the caller's value, else the
/// argument's default. A missing required argument is an invalid request.
pub fn prompt(name: &str, given: Option<&JsonObject>) -> Result<GetPromptResult, McpError> {
    let Some(entry) = entries("prompts").iter().find(|entry| text(entry, "name") == name) else {
        return Err(McpError::invalid_params(format!("no prompt '{name}'"), None));
    };
    let mut body = text(entry, "template").to_string();
    for arg in entry["arguments"].as_array().map(Vec::as_slice).unwrap_or_default() {
        let key = text(arg, "name");
        let value = given.and_then(|g| g.get(key)).and_then(Value::as_str);
        let value = value.filter(|v| !v.is_empty());
        let value = match (value, arg["required"].as_bool().unwrap_or(false)) {
            (Some(value), _) => value,
            (None, false) => text(arg, "default"),
            (None, true) => {
                return Err(McpError::invalid_params(format!("prompt '{name}' needs `{key}`"), None))
            }
        };
        body = body.replace(&format!("{{{key}}}"), value);
    }
    let message = PromptMessage::new_text(PromptMessageRole::User, body);
    let mut result = GetPromptResult::new(vec![message]);
    result.description = Some(text(entry, "description").to_string());
    Ok(result)
}
