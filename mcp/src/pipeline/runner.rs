//! The one place this crate spawns a process.

use std::borrow::Cow;
use std::path::Path;

use crate::locate;

/// Raw capture from one CLI invocation.
pub struct CliOutput {
    pub success: bool,
    pub stderr: String,
}

/// One CLI invocation: argv in, exit status + stderr out. [`ProcessRunner`] spawns the real
/// binary; a test substitutes its own runner and needs no Zig toolchain.
pub trait CliRunner: Sync {
    fn run(
        &self,
        argv: &[Cow<'static, str>],
        dir: Option<&Path>,
    ) -> impl std::future::Future<Output = Result<CliOutput, String>> + Send;
}

/// The real runner.
pub struct ProcessRunner;

impl CliRunner for ProcessRunner {
    async fn run(
        &self,
        argv: &[Cow<'static, str>],
        dir: Option<&Path>,
    ) -> Result<CliOutput, String> {
        spawn(argv, dir).await
    }
}

/// Locate the CLI and run it with the given argv under the `config::cli_timeout()`
/// deadline. Its stdin is `/dev/null`: ours is the JSON-RPC channel.
async fn spawn(argv: &[Cow<'static, str>], dir: Option<&Path>) -> Result<CliOutput, String> {
    let bin = locate::find_cli()?;
    let deadline = crate::config::cli_timeout();
    let failed = |e| format!("failed to run the stencil CLI ({}): {e}", bin.display());
    let child = tokio::process::Command::new(&bin)
        // `.` is the inherited working directory; a confined run names its sandbox root.
        .current_dir(dir.unwrap_or(Path::new(".")))
        .args(argv.iter().map(|token| token.as_ref()))
        .env("NO_COLOR", "1")
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        // On expiry the timeout drops the wait future, which drops the child; this kills it.
        .kill_on_drop(true)
        .spawn()
        .map_err(failed)?;

    let output = match tokio::time::timeout(deadline, child.wait_with_output()).await {
        Ok(result) => result.map_err(failed)?,
        Err(_) => {
            return Err(format!(
                "error: the stencil CLI timed out after {}s and was terminated",
                deadline.as_secs()
            ))
        }
    };
    Ok(CliOutput {
        success: output.status.success(),
        stderr: String::from_utf8_lossy(&output.stderr).into_owned(),
    })
}
