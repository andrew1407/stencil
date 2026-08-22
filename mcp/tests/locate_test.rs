//! Discovering the CLI binary the server shells out to.
//!
//! `find_cli` has a documented precedence — STENCIL_CLI, then the repo checkout, then PATH —
//! and the first step must be authoritative: an operator who points STENCIL_CLI at the wrong
//! path should get a clear error, never a silent fall-through to some other `stencil` that
//! happens to be on PATH.
//!
//! These tests mutate process-wide environment variables, so they serialize on a lock. They
//! live in their own test binary (one binary per `tests/*.rs`), so the lock covers every
//! test that can observe the change.

use std::path::PathBuf;
use std::sync::{Mutex, MutexGuard, OnceLock};

use stencil_mcp::locate::{find_cli, missing_message, repo_root};

fn env_lock() -> MutexGuard<'static, ()> {
    static LOCK: OnceLock<Mutex<()>> = OnceLock::new();
    // A poisoned lock just means an earlier test panicked; the env is restored by the
    // guard below either way, so recover rather than cascade the failure.
    match LOCK.get_or_init(|| Mutex::new(())).lock() {
        Ok(guard) => guard,
        Err(poisoned) => poisoned.into_inner(),
    }
}

/// Sets STENCIL_CLI for as long as it is held, restoring the previous value on drop so one
/// test can never leak into another.
struct StencilCliEnv {
    previous: Option<std::ffi::OsString>,
    _guard: MutexGuard<'static, ()>,
}

impl StencilCliEnv {
    fn set(value: Option<&std::path::Path>) -> Self {
        let guard = env_lock();
        let previous = std::env::var_os("STENCIL_CLI");
        match value {
            Some(path) => std::env::set_var("STENCIL_CLI", path),
            None => std::env::remove_var("STENCIL_CLI"),
        }
        Self {
            previous,
            _guard: guard,
        }
    }
}

impl Drop for StencilCliEnv {
    fn drop(&mut self) {
        match self.previous.take() {
            Some(prev) => std::env::set_var("STENCIL_CLI", prev),
            None => std::env::remove_var("STENCIL_CLI"),
        }
    }
}

/// An explicit override wins outright, whether or not a repo build or a PATH binary exists.
#[test]
fn stencil_cli_env_var_takes_precedence() {
    let file = tempfile::NamedTempFile::new().expect("temp file");
    let _env = StencilCliEnv::set(Some(file.path()));

    let found = find_cli().expect("an existing STENCIL_CLI file resolves");
    assert_eq!(found, file.path());
}

/// A misconfigured override is an error naming the bad path — NOT a quiet fall-through to
/// the repo build or PATH, which would run a different binary than the operator asked for.
#[test]
fn a_missing_stencil_cli_path_is_an_error_not_a_fallback() {
    let missing = PathBuf::from("/nonexistent/stencil-cli-that-is-not-there");
    let _env = StencilCliEnv::set(Some(&missing));

    let err = find_cli().expect_err("a non-existent STENCIL_CLI must fail");
    assert!(
        err.contains(&missing.display().to_string()),
        "the error should name the bad path, got: {err}"
    );
    assert!(
        err.contains("not a file"),
        "the error should say why, got: {err}"
    );
}

/// A directory is not a runnable binary; `is_file` must reject it rather than hand back a
/// path the spawn would fail on with a much less obvious message.
#[test]
fn a_directory_is_rejected_as_the_stencil_cli_path() {
    let dir = tempfile::tempdir().expect("temp dir");
    let _env = StencilCliEnv::set(Some(dir.path()));

    let err = find_cli().expect_err("a directory must not resolve as the CLI binary");
    assert!(
        err.contains("not a file"),
        "expected a not-a-file error, got: {err}"
    );
}

/// With no override, resolution falls back to the repo checkout / PATH. The test process
/// runs from inside the checkout, so the repo root is discoverable and — since CI builds the
/// CLI before running these tests — resolution succeeds. When it does not (a fresh clone
/// with no `zig build` and no `stencil` on PATH), the error must be the actionable one.
#[test]
fn without_an_override_it_finds_the_repo_build_or_explains_itself() {
    let _env = StencilCliEnv::set(None);

    match find_cli() {
        Ok(path) => assert!(
            path.is_file(),
            "a resolved CLI path must be an existing file: {}",
            path.display()
        ),
        Err(err) => assert_eq!(
            err,
            missing_message(),
            "an unresolved CLI must produce the actionable message"
        ),
    }
}

/// The tests run from `mcp/`, inside the checkout, so the repo root is the nearest ancestor
/// holding `cli/build.zig`. Other modules derive sibling paths from it.
#[test]
fn repo_root_finds_the_checkout_containing_the_cli_build() {
    let root = repo_root().expect("the tests run inside the repo checkout");
    assert!(
        root.join("cli/build.zig").is_file(),
        "repo_root returned {} which has no cli/build.zig",
        root.display()
    );
    // It really is an ancestor of this crate, not some unrelated match.
    assert!(root.join("mcp/Cargo.toml").is_file());
}

/// The message is what an operator sees when nothing resolved, so it has to name all three
/// escape hatches. A reworded message that drops one is a real regression in usability.
#[test]
fn missing_message_names_every_way_out() {
    let msg = missing_message();
    for needle in ["stencil", "zig build", "STENCIL_CLI", "Docker"] {
        assert!(
            msg.contains(needle),
            "the missing-CLI message should mention {needle:?}, got: {msg}"
        );
    }
}
