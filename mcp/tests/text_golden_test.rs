//! Byte-exact goldens for the MCP server's user-facing prose: every `#[tool]` description
//! and the `ServerHandler::get_info()` instructions. That text is slated to
//! move into JSON assets, so these pin the current bytes to prove the move is verbatim.
//! Rewrite with `MCP_UPDATE_GOLDENS=1 cargo test`.

use rmcp::ServerHandler;
use stencil_mcp::server::StencilServer;

const GOLDENS_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/goldens");

/// Compare against `tests/goldens/{name}`, or rewrite it when `MCP_UPDATE_GOLDENS=1`.
fn check(name: &str, actual: &str) {
    let path = format!("{GOLDENS_DIR}/{name}");
    if std::env::var("MCP_UPDATE_GOLDENS").as_deref() == Ok("1") {
        std::fs::create_dir_all(GOLDENS_DIR).expect("create the goldens dir");
        std::fs::write(&path, actual).unwrap_or_else(|e| panic!("cannot write {path}: {e}"));
        return;
    }
    let expected = std::fs::read_to_string(&path).unwrap_or_else(|e| {
        panic!("cannot read {path}: {e} — rerun with MCP_UPDATE_GOLDENS=1")
    });
    if expected == actual {
        return;
    }
    panic!(
        "golden {name} differs (MCP_UPDATE_GOLDENS=1 to rewrite):\n{}",
        diff(&expected, actual)
    );
}

/// The first few differing lines, so a failure names the wording that moved.
fn diff(expected: &str, actual: &str) -> String {
    let want: Vec<&str> = expected.lines().collect();
    let got: Vec<&str> = actual.lines().collect();
    let mut out = String::new();
    let mut shown = 0;
    for i in 0..want.len().max(got.len()) {
        let (w, g) = (want.get(i).unwrap_or(&"<eof>"), got.get(i).unwrap_or(&"<eof>"));
        if w == g || shown >= 12 {
            continue;
        }
        out.push_str(&format!("  line {}:\n    want {w}\n    got  {g}\n", i + 1));
        shown += 1;
    }
    if shown == 0 {
        out.push_str(&format!(
            "  identical line-wise; lengths {} vs {}\n",
            expected.len(),
            actual.len()
        ));
    }
    out
}

/// The description the `#[tool]` attribute registered, as an MCP client sees it.
fn description(tool: rmcp::model::Tool) -> String {
    tool.description
        .unwrap_or_else(|| panic!("tool \"{}\" has no description", tool.name))
        .to_string()
}

#[test]
fn the_tool_descriptions_match_their_goldens() {
    check(
        "stencil_edit.txt",
        &description(StencilServer::stencil_edit_tool_attr()),
    );
    check(
        "stencil_probe.txt",
        &description(StencilServer::stencil_probe_tool_attr()),
    );
    check(
        "stencil_prompt.txt",
        &description(StencilServer::stencil_prompt_tool_attr()),
    );
    check(
        "stencil_script.txt",
        &description(StencilServer::stencil_script_tool_attr()),
    );
    check(
        "source_site.txt",
        &description(StencilServer::source_site_tool_attr()),
    );
}

#[test]
fn the_server_instructions_match_their_golden() {
    let info = StencilServer::default().get_info();
    check(
        "instructions.txt",
        &info.instructions.expect("get_info() carries instructions"),
    );
}
