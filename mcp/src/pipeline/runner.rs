//! The one place this crate spawns a CLI process: a bounded number at once, a scrubbed
//! environment, and pipes read without letting them grow unbounded.

use std::borrow::Cow;
use std::path::Path;
use std::process::Stdio;
use std::sync::LazyLock;

use tokio::io::AsyncWriteExt;
use tokio::sync::Semaphore;

use super::capture::{read_capped, read_lines, STDERR_TAIL, STDOUT_CAP};
use super::env::child_env;
use super::progress;
use crate::locate;

/// Raw capture from one CLI invocation; `stdout` is empty unless the run captured it.
pub struct CliOutput {
    pub success: bool,
    pub stderr: String,
    pub stdout: String,
}

/// One CLI invocation: argv in, exit status + stderr out. [`ProcessRunner`] spawns the real
/// binary; a test substitutes its own runner and needs no Zig toolchain.
pub trait CliRunner: Sync {
    fn run(
        &self,
        argv: &[Cow<'static, str>],
        dir: Option<&Path>,
    ) -> impl std::future::Future<Output = Result<CliOutput, String>> + Send;

    /// The same run with stdout captured too — only the modes that print a result there.
    fn run_capturing(
        &self,
        argv: &[Cow<'static, str>],
        dir: Option<&Path>,
    ) -> impl std::future::Future<Output = Result<CliOutput, String>> + Send {
        self.run(argv, dir)
    }

    /// `run_capturing` with `input` on the child's stdin — the reply `--plan-check -` reads.
    fn run_feeding(
        &self,
        argv: &[Cow<'static, str>],
        input: &str,
    ) -> impl std::future::Future<Output = Result<CliOutput, String>> + Send {
        let _ = input;
        self.run_capturing(argv, None)
    }

    /// `run` (or `run_capturing`) with `env` added to the child's allowlisted environment —
    /// the server tokens a run needs. A runner that has no child may ignore it.
    fn run_with(
        &self,
        argv: &[Cow<'static, str>],
        dir: Option<&Path>,
        env: &[(&'static str, String)],
        capture: bool,
    ) -> impl std::future::Future<Output = Result<CliOutput, String>> + Send {
        let _ = env;
        async move {
            match capture {
                true => self.run_capturing(argv, dir).await,
                false => self.run(argv, dir).await,
            }
        }
    }
}

/// The real runner.
pub struct ProcessRunner;

impl CliRunner for ProcessRunner {
    async fn run(
        &self,
        argv: &[Cow<'static, str>],
        dir: Option<&Path>,
    ) -> Result<CliOutput, String> {
        spawn(argv, dir, &[], false, None).await
    }

    async fn run_capturing(
        &self,
        argv: &[Cow<'static, str>],
        dir: Option<&Path>,
    ) -> Result<CliOutput, String> {
        spawn(argv, dir, &[], true, None).await
    }

    async fn run_feeding(
        &self,
        argv: &[Cow<'static, str>],
        input: &str,
    ) -> Result<CliOutput, String> {
        spawn(argv, None, &[], true, Some(input)).await
    }

    async fn run_with(
        &self,
        argv: &[Cow<'static, str>],
        dir: Option<&Path>,
        env: &[(&'static str, String)],
        capture: bool,
    ) -> Result<CliOutput, String> {
        spawn(argv, dir, env, capture, None).await
    }
}

/// The variable a CLI child reads its server tokens from (`cli/CONTRACT.md` §1): `origin=token`
/// pairs, so neither the argv nor a `ps` listing ever shows one.
pub const SERVER_TOKENS: &str = "STENCIL_SERVER_TOKENS";

/// Every CLI child in the process shares these slots: each one decodes a whole image.
static SLOTS: LazyLock<Semaphore> =
    LazyLock::new(|| Semaphore::new(crate::config::max_concurrent_cli()));

/// Locate the CLI and run it under the `config::cli_timeout()` deadline once a slot frees.
/// Its stdin is `/dev/null`, or a pipe carrying `input`: ours is the JSON-RPC channel.
async fn spawn(
    argv: &[Cow<'static, str>],
    dir: Option<&Path>,
    env: &[(&'static str, String)],
    capture_stdout: bool,
    input: Option<&str>,
) -> Result<CliOutput, String> {
    let bin = locate::find_cli()?;
    let deadline = crate::config::cli_timeout();
    let failed = |e| format!("failed to run the stencil CLI ({}): {e}", bin.display());
    let _slot = match SLOTS.try_acquire() {
        Ok(slot) => slot,
        Err(_) => {
            progress::report("waiting for a free stencil CLI slot");
            SLOTS.acquire().await.map_err(|e| e.to_string())?
        }
    };
    let piped = |on: bool| if on { Stdio::piped() } else { Stdio::null() };
    let mut child = tokio::process::Command::new(&bin)
        // `.` is the inherited working directory; a confined run names its sandbox root.
        .current_dir(dir.unwrap_or(Path::new(".")))
        .args(argv.iter().map(|token| token.as_ref()))
        .env_clear()
        .envs(child_env(std::env::vars_os()))
        .envs(env.iter().map(|(name, value)| (*name, value.as_str())))
        .env("NO_COLOR", "1")
        .stdin(piped(input.is_some()))
        .stdout(piped(capture_stdout))
        .stderr(Stdio::piped())
        // On expiry the timeout drops the run future, which drops the child; this kills it.
        .kill_on_drop(true)
        .spawn()
        .map_err(failed)?;

    let (stderr, stdout, stdin) = (child.stderr.take(), child.stdout.take(), child.stdin.take());
    let run = async move {
        // A child that exits before reading it all closes the pipe; its exit status tells why.
        let feed = async {
            if let (Some(mut pipe), Some(input)) = (stdin, input) {
                let _ = pipe.write_all(input.as_bytes()).await;
            }
        };
        let err = async {
            match stderr {
                Some(pipe) => read_lines(pipe, STDERR_TAIL, report_step).await,
                None => String::new(),
            }
        };
        let out = async {
            match stdout {
                Some(pipe) => read_capped(pipe, STDOUT_CAP).await,
                None => Ok(String::new()),
            }
        };
        let (stderr, stdout, ()) = tokio::join!(err, out, feed);
        (child.wait().await, stderr, stdout)
    };
    let Ok((status, stderr, stdout)) = tokio::time::timeout(deadline, run).await else {
        return Err(format!(
            "error: the stencil CLI timed out after {}s and was terminated",
            deadline.as_secs()
        ));
    };
    let status = status.map_err(failed)?;
    Ok(CliOutput { success: status.success(), stderr, stdout: stdout? })
}

/// A written file or a scrape milestone is a step worth telling the client about.
fn report_step(line: &str) {
    let line = line.trim();
    if ["wrote ", "scraped ", "scraping "].iter().any(|p| line.starts_with(p)) {
        progress::report(line);
    }
}
