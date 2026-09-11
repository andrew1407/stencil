//! The shared fixture and the CLI-present gate the end-to-end suites self-skip on.
use stencil_mcp::args::EditParams;
use stencil_mcp::locate;

/// The 16x12 PNG fixture shared with the CLI's own test suite.
pub const FIXTURE: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../cli/tests/fixtures/sample.png"
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
