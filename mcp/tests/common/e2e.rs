//! The shared fixture and the CLI-present gate the end-to-end suites self-skip on.
use stencil_mcp::args::EditParams;
use stencil_mcp::locate;

/// The 16x12 PNG fixture shared with the CLI's own test suite.
pub const FIXTURE: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../common/samples/sample.png"
);

pub fn cli_present() -> bool {
    if locate::find_cli().is_err() {
        eprintln!("skipping e2e: stencil CLI not found (build it in cli/ or set STENCIL_CLI)");
        return false;
    }
    true
}

pub fn edit_params(value: serde_json::Value) -> EditParams {
    serde_json::from_value(value).expect("params should deserialize")
}

/// Edit params confined to `root`, as the tool layer confines every run.
pub fn confined_params(value: serde_json::Value, root: &std::path::Path) -> EditParams {
    let mut params = edit_params(value);
    params.confine_root = Some(root.to_string_lossy().into_owned());
    params
}
