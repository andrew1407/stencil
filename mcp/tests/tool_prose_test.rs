//! The drift test for `toolDescriptions.json`, this surface's one home for user-facing prose.
//!
//! Two consumers are generated from it and committed: the `toolDescriptions/*.txt` shards the
//! `#[tool]` descriptions `include_str!` (rmcp's macro takes a literal, not an expression) and
//! README.md's Tools table. Both are byte-compared here; `MCP_UPDATE_PROSE=1 cargo test`
//! rewrites them. `text_golden_test.rs` then pins what the wire actually carries.

use serde_json::Value;

const ROOT: &str = env!("CARGO_MANIFEST_DIR");
const START: &str = "<!-- generated from toolDescriptions.json";
const END: &str = "<!-- /generated -->";

fn updating() -> bool {
    std::env::var("MCP_UPDATE_PROSE").as_deref() == Ok("1")
}

fn read(path: &str) -> String {
    std::fs::read_to_string(format!("{ROOT}/{path}"))
        .unwrap_or_else(|e| panic!("cannot read mcp/{path}: {e} — MCP_UPDATE_PROSE=1 to write it"))
}

/// Compare `mcp/{path}` with `wanted`, or rewrite it when updating.
fn check(path: &str, wanted: &str) {
    let full = format!("{ROOT}/{path}");
    if updating() {
        if let Some(dir) = std::path::Path::new(&full).parent() {
            std::fs::create_dir_all(dir).expect("create the asset directory");
        }
        std::fs::write(&full, wanted).unwrap_or_else(|e| panic!("cannot write mcp/{path}: {e}"));
        return;
    }
    let found = read(path);
    assert!(
        found == wanted,
        "mcp/{path} is stale against toolDescriptions.json \
         (MCP_UPDATE_PROSE=1 cargo test to rewrite):\n  have {} bytes, want {} bytes",
        found.len(),
        wanted.len()
    );
}

fn asset() -> Value {
    serde_json::from_str(&read("toolDescriptions.json")).expect("toolDescriptions.json is JSON")
}

fn tools(asset: &Value) -> Vec<&Value> {
    asset["tools"]
        .as_array()
        .expect("toolDescriptions.json: \"tools\" is an array")
        .iter()
        .collect()
}

fn field<'a>(tool: &'a Value, key: &str) -> &'a str {
    tool[key]
        .as_str()
        .unwrap_or_else(|| panic!("toolDescriptions.json: every tool needs a string \"{key}\""))
}

#[test]
fn the_description_shards_match_the_asset() {
    let asset = asset();
    for tool in tools(&asset) {
        let name = field(tool, "name");
        check(
            &format!("toolDescriptions/{name}.txt"),
            field(tool, "description"),
        );
    }
}

#[test]
fn the_readme_tools_table_matches_the_asset() {
    let asset = asset();
    let mut table = String::from("| Tool | Purpose | Key parameters |\n|---|---|---|\n");
    for tool in tools(&asset) {
        table.push_str(&format!(
            "| `{}` | {} | {} |\n",
            field(tool, "name"),
            field(tool, "readmePurpose"),
            field(tool, "readmeParams")
        ));
    }

    let readme = read("README.md");
    let open = readme.find(START).expect("README.md carries the generated marker");
    let body = readme[open..].find('\n').expect("a marker line") + open + 1;
    let close = readme[body..].find(END).expect("README.md closes the generated block") + body;
    if readme[body..close] == table {
        return;
    }
    assert!(
        updating(),
        "README.md's Tools table is stale against toolDescriptions.json \
         (MCP_UPDATE_PROSE=1 cargo test to rewrite)"
    );
    let rewritten = format!("{}{table}{}", &readme[..body], &readme[close..]);
    std::fs::write(format!("{ROOT}/README.md"), rewritten).expect("rewrite README.md");
}

#[test]
fn every_tool_the_server_exposes_has_one_entry() {
    let asset = asset();
    let mut names: Vec<&str> = tools(&asset).iter().map(|t| field(t, "name")).collect();
    names.sort_unstable();
    assert_eq!(
        names,
        ["source_site", "stencil_edit", "stencil_probe", "stencil_prompt"]
    );
    assert!(
        !asset["instructions"]
            .as_str()
            .expect("toolDescriptions.json: \"instructions\" is a string")
            .is_empty()
    );
}
