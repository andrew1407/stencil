//! Discover the Stencil CLI binary the server shells out to.
//!
//! Resolution order (first hit wins):
//!   1. `STENCIL_CLI` env var — an explicit path override.
//!   2. The repo checkout — walk up from the CWD *and* the running executable for the
//!      nearest ancestor containing `cli/build.zig`, then `cli/zig-out/bin/stencil`.
//!   3. `stencil` on `PATH`.
//!
//! The server never builds the CLI itself — it stays side-effect-free and reports a
//! clear, actionable error when the binary is missing.

use std::path::{Path, PathBuf};

const BINARY_NAME: &str = "stencil";

/// The relative path of the built CLI inside a repo checkout.
const REPO_BINARY: &str = "cli/zig-out/bin/stencil";

/// A marker that identifies the repo root unambiguously.
const REPO_MARKER: &str = "cli/build.zig";

pub fn missing_message() -> String {
    format!(
        "could not find the `{BINARY_NAME}` CLI. Build it with `zig build` in `cli/`, \
         set the STENCIL_CLI env var to its path, or run the Docker image."
    )
}

/// Resolve the CLI binary path, or return an actionable error message.
pub fn find_cli() -> Result<PathBuf, String> {
    if let Some(env) = std::env::var_os("STENCIL_CLI") {
        let path = PathBuf::from(env);
        if path.is_file() {
            return Ok(path);
        }
        return Err(format!(
            "STENCIL_CLI is set to '{}', which is not a file",
            path.display()
        ));
    }

    if let Some(path) = find_in_repo() {
        return Ok(path);
    }

    if let Some(path) = find_on_path() {
        return Ok(path);
    }

    Err(missing_message())
}

/// Find the repo root (nearest ancestor with `cli/build.zig`) above the CWD or the running
/// executable. Other modules use this to derive sibling paths (e.g. the desktop binary).
pub fn repo_root() -> Option<PathBuf> {
    for start in start_dirs() {
        if let Some(root) = repo_root_from(&start) {
            return Some(root);
        }
    }
    None
}

/// Candidate directories to start an upward search from.
fn start_dirs() -> Vec<PathBuf> {
    let mut starts: Vec<PathBuf> = Vec::new();
    if let Ok(cwd) = std::env::current_dir() {
        starts.push(cwd);
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            starts.push(dir.to_path_buf());
        }
    }
    starts
}

/// Look for `cli/zig-out/bin/stencil` under the nearest repo root above the CWD or the
/// running executable.
fn find_in_repo() -> Option<PathBuf> {
    for start in start_dirs() {
        if let Some(root) = repo_root_from(&start) {
            let candidate = root.join(REPO_BINARY);
            if candidate.is_file() {
                return Some(candidate);
            }
        }
    }
    None
}

/// Walk up from `start` looking for an ancestor that contains `cli/build.zig`.
fn repo_root_from(start: &Path) -> Option<PathBuf> {
    let mut dir = Some(start);
    while let Some(d) = dir {
        if d.join(REPO_MARKER).is_file() {
            return Some(d.to_path_buf());
        }
        dir = d.parent();
    }
    None
}

/// Scan `PATH` for an executable named `stencil`.
fn find_on_path() -> Option<PathBuf> {
    let path = std::env::var_os("PATH")?;
    for dir in std::env::split_paths(&path) {
        let candidate = dir.join(BINARY_NAME);
        if candidate.is_file() {
            return Some(candidate);
        }
    }
    None
}
