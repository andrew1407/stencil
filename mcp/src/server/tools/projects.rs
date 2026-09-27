//! `stencil_projects`: the projects on an allowlisted collaboration server — the names
//! `stencil_edit`'s `server` + `input` fetch by — or one project's metadata (and the files it
//! stores), read through the CLI's `--list-projects` / `--project-info` / `--project-files`.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use serde_json::Value;

use super::{err_result, ok_result};
use crate::args::{self, ProjectsParams};
use crate::config::{ServerEntry, Servers};
use crate::pipeline;

/// A project's public metadata, as the server's REST API spells it; the stored image and
/// layout never ride along.
#[derive(Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "camelCase")]
pub struct ProjectMeta {
    id: String,
    name: String,
    #[serde(default)]
    created_at: i64,
    #[serde(default)]
    updated_at: i64,
    #[serde(default)]
    has_image: bool,
    #[serde(default)]
    image_w: u32,
    #[serde(default)]
    image_h: u32,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    description: Option<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    keywords: Vec<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    source: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    color: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    blank_color: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    expires_at: Option<i64>,
    #[serde(default)]
    version: i64,
    /// Only for a read asked with `files`: each kind of file the project stores.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    files: Option<Vec<StoredFile>>,
}

impl ProjectMeta {
    /// `"name" (id) WxH`, as every summary names a project.
    pub(super) fn line(&self) -> String {
        format!("\"{}\" ({}) {}x{}", self.name, self.id, self.image_w, self.image_h)
    }

    pub(super) fn version(&self) -> i64 {
        self.version
    }
}

/// One file a project stores: its kind and the format its first bytes name, when known.
#[derive(Serialize, Deserialize, JsonSchema)]
pub struct StoredFile {
    kind: String,
    format: Option<String>,
}

/// The `stencil_projects` payload: the server asked, the projects (one for a read by id),
/// and the cursor of the next page when a listing has more.
#[derive(Serialize, JsonSchema)]
pub struct ProjectsPayload {
    server: String,
    projects: Vec<ProjectMeta>,
    next_cursor: Option<String>,
}

const DEFAULT_LIMIT: u32 = 50;
const MAX_LIMIT: u32 = 500;

pub async fn run(servers: &Servers, params: ProjectsParams) -> Result<CallToolResult, McpError> {
    let entry = match request(servers, &params) {
        Ok(entry) => entry,
        Err(message) => return Ok(err_result(message)),
    };
    let argv = match (params.id.as_deref(), params.files) {
        (Some(id), true) => args::build_project_files_argv(&entry.origin, id),
        (id, _) => args::build_projects_argv(&entry.origin, id),
    };
    let argv = match argv {
        Ok(argv) => argv,
        Err(error) => return Ok(err_result(error.to_string())),
    };
    let tokens = Servers::child_tokens([entry]);
    let document = match pipeline::run_projects(&argv, tokens.as_deref()).await {
        Ok(document) => document,
        Err(message) => return Ok(err_result(message)),
    };
    let (projects, next_cursor) = match params.id {
        Some(_) => (vec![document], None),
        None => match page(document, &params) {
            Ok(page) => page,
            Err(message) => return Ok(err_result(message)),
        },
    };
    let projects: Result<Vec<ProjectMeta>, _> =
        projects.into_iter().map(serde_json::from_value).collect();
    let projects = match projects {
        Ok(projects) => projects,
        Err(e) => {
            let origin = &entry.origin;
            return Ok(err_result(format!("{origin} sent a project this tool cannot read: {e}")));
        }
    };

    let mut summary = format!("{} project(s) on {}", projects.len(), entry.origin);
    for p in &projects {
        summary.push_str(&format!("\n- {}", p.line()));
        for file in p.files.iter().flatten() {
            let format = file.format.as_deref().unwrap_or("unknown format");
            summary.push_str(&format!("\n  - {} ({format})", file.kind));
        }
    }
    if let Some(cursor) = &next_cursor {
        summary.push_str(&format!("\nmore: pass after=\"{cursor}\""));
    }
    ok_result(summary, ProjectsPayload { server: entry.origin.clone(), projects, next_cursor })
}

/// The allowlisted entry a call names — the only one when it names none — or why not.
pub(super) fn entry<'a>(
    servers: &'a Servers,
    server: Option<&str>,
) -> Result<&'a ServerEntry, String> {
    match server {
        Some(url) => servers.find("server", url),
        None => match servers.origins().as_slice() {
            [only] => servers.find("server", only),
            [] => servers.find("server", ""),
            many => Err(format!("`server` must name one of: {}", many.join(", "))),
        },
    }
}

/// The allowlisted entry the call names, or why it is refused.
fn request<'a>(servers: &'a Servers, params: &ProjectsParams) -> Result<&'a ServerEntry, String> {
    let entry = entry(servers, params.server.as_deref())?;
    if let Some(id) = &params.id {
        args::check_project_id(id).map_err(|e| e.to_string())?;
    }
    if params.files && params.id.is_none() {
        return Err("`files` lists one project's files — pass its `id`".to_string());
    }
    let limit = params.limit.unwrap_or(DEFAULT_LIMIT);
    if !(1..=MAX_LIMIT).contains(&limit) {
        return Err(format!("`limit` must be 1..{MAX_LIMIT}"));
    }
    Ok(entry)
}

/// One page of the CLI's whole listing: `limit` projects after the one whose id is `after`,
/// and that page's last id as the next cursor while more follow.
fn page(listing: Value, params: &ProjectsParams) -> Result<(Vec<Value>, Option<String>), String> {
    let Value::Array(all) = listing else {
        return Err("the stencil CLI's project listing is not an array".to_string());
    };
    let start = match &params.after {
        None => 0,
        Some(after) => match all.iter().position(|p| p["id"] == after.as_str()) {
            Some(at) => at + 1,
            None => return Err(format!("`after` \"{after}\" is no longer a project on this server")),
        },
    };
    let limit = params.limit.unwrap_or(DEFAULT_LIMIT) as usize;
    let end = (start + limit).min(all.len());
    let rest = end < all.len();
    let projects: Vec<Value> = all.into_iter().skip(start).take(end - start).collect();
    let cursor = projects.last().filter(|_| rest).and_then(|p| p["id"].as_str()).map(String::from);
    Ok((projects, cursor))
}

#[cfg(test)]
mod tests {
    //! The paging over the CLI's whole listing (pure): a cursor is the last id of a page.

    use super::*;
    use serde_json::json;

    fn listing(n: usize) -> Value {
        Value::Array((0..n).map(|i| json!({ "id": format!("p_{i}_x"), "name": "N" })).collect())
    }

    fn params(limit: u32, after: Option<&str>) -> ProjectsParams {
        ProjectsParams { limit: Some(limit), after: after.map(String::from), ..Default::default() }
    }

    fn ids(page: &[Value]) -> Vec<&str> {
        page.iter().map(|p| p["id"].as_str().unwrap()).collect()
    }

    #[test]
    fn a_listing_pages_by_the_last_id_until_it_runs_out() {
        let (first, cursor) = page(listing(5), &params(2, None)).unwrap();
        assert_eq!(ids(&first), ["p_0_x", "p_1_x"]);
        assert_eq!(cursor.as_deref(), Some("p_1_x"));
        let (last, cursor) = page(listing(5), &params(3, Some("p_1_x"))).unwrap();
        assert_eq!(ids(&last), ["p_2_x", "p_3_x", "p_4_x"]);
        assert_eq!(cursor, None, "nothing follows the last page");
    }

    #[test]
    fn a_cursor_that_names_no_project_and_a_non_array_are_refused() {
        let gone = page(listing(3), &params(2, Some("p_9_x"))).unwrap_err();
        assert!(gone.contains("no longer a project"), "{gone}");
        assert!(page(json!({ "id": "p_1_x" }), &params(2, None)).is_err());
    }
}
