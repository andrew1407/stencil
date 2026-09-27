//! Discover the Stencil CLI binary the server shells out to.
//!
//! Resolution order: `STENCIL_CLI`, then the nearest ancestor of the running executable
//! holding `cli/build.zig`, then `stencil` on `PATH`. Never the CWD: a workspace must not be
//! able to plant the binary this server runs. A hit is cached; the override is re-read.

use std::path::{Path, PathBuf};
use std::sync::OnceLock;

const BINARY_NAME: &str = "stencil";

/// The relative path of the built CLI inside a repo checkout.
const REPO_BINARY: &str = "cli/zig-out/bin/stencil";

/// A sentinel path that identifies the repo root unambiguously.
const REPO_SENTINEL: &str = "cli/build.zig";

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

    // Only a HIT is remembered: a miss stays live, so a CLI built while the server runs
    // still resolves.
    static RESOLVED: OnceLock<PathBuf> = OnceLock::new();
    if let Some(found) = RESOLVED.get() {
        return Ok(found.clone());
    }
    let found = find_in_repo().or_else(find_on_path).ok_or_else(missing_message)?;
    Ok(RESOLVED.get_or_init(|| found).clone())
}

/// Find the repo root (nearest ancestor with `cli/build.zig`) above the running executable.
/// Other modules use this to derive sibling paths (e.g. the desktop binary).
pub fn repo_root() -> Option<PathBuf> {
    let exe = std::env::current_exe().ok()?;
    repo_root_from(exe.parent()?)
}

/// Look for `cli/zig-out/bin/stencil` under the repo root above the running executable.
fn find_in_repo() -> Option<PathBuf> {
    let candidate = repo_root()?.join(REPO_BINARY);
    candidate.is_file().then_some(candidate)
}

/// Walk up from `start` looking for an ancestor that contains `cli/build.zig`.
pub fn repo_root_from(start: &Path) -> Option<PathBuf> {
    let mut dir = Some(start);
    while let Some(d) = dir {
        if d.join(REPO_SENTINEL).is_file() {
            return Some(d.to_path_buf());
        }
        dir = d.parent();
    }
    None
}

/// Scan `PATH` for an executable named `stencil`.
fn find_on_path() -> Option<PathBuf> {
    search_path(&std::env::var_os("PATH")?)
}

/// The first `stencil` in the `PATH`-shaped list `path`. A relative entry (`.`, or an empty
/// one) would resolve against the CWD, so it is skipped.
pub fn search_path(path: &std::ffi::OsStr) -> Option<PathBuf> {
    std::env::split_paths(path)
        .filter(|dir| dir.is_absolute())
        .map(|dir| dir.join(BINARY_NAME))
        .find(|candidate| candidate.is_file())
}
