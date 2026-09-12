//! Reading configuration out of the environment: the per-spawn CLI deadline, argv lookups,
//! and the hand-rolled dotenv loader, which never overrides what the process env already says.
use std::path::PathBuf;
use std::time::Duration;

/// Read an env var, treating unset and blank the same.
pub(super) fn env_nonempty(key: &str) -> Option<String> {
    std::env::var(key)
        .ok()
        .map(|v| v.trim().to_string())
        .filter(|v| !v.is_empty())
}

/// Per-invocation deadline for one CLI run (`STENCIL_CLI_TIMEOUT_SECONDS`, default 120s —
/// the bot's `STENCIL_BOT_CLI_TIMEOUT_SECONDS` default): a hung CLI must never pin an MCP
/// tool call forever. Read per spawn; a blank/zero/unparseable value falls back to 120s.
pub fn cli_timeout() -> Duration {
    let secs = env_nonempty("STENCIL_CLI_TIMEOUT_SECONDS").and_then(|v| v.parse::<u64>().ok());
    Duration::from_secs(secs.filter(|s| *s > 0).unwrap_or(120))
}

/// The default desktop binary location inside a repo checkout.
pub(super) fn default_desktop_path() -> Option<PathBuf> {
    let candidate = crate::locate::repo_root()?.join("desktop/build/stencil");
    candidate.is_file().then_some(candidate)
}

/// Read `--flag value` out of an argv slice.
pub(super) fn arg_value(args: &[String], flag: &str) -> Option<String> {
    let pos = args.iter().position(|a| a == flag)?;
    args.get(pos + 1).cloned()
}

/// Minimal dotenv loader: `KEY=VALUE` lines, never overriding the process env.
pub(super) fn load_dotenv() {
    let Some(path) = dotenv_path() else {
        return;
    };
    let Ok(contents) = std::fs::read_to_string(&path) else {
        return;
    };
    for line in contents.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let line = line.strip_prefix("export ").unwrap_or(line);
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        let key = key.trim();
        if key.is_empty() || std::env::var_os(key).is_some() {
            continue;
        }
        // Strip matching surrounding quotes, if any.
        let value = value.trim();
        let value = value
            .strip_prefix('"')
            .and_then(|v| v.strip_suffix('"'))
            .or_else(|| value.strip_prefix('\'').and_then(|v| v.strip_suffix('\'')))
            .unwrap_or(value);
        std::env::set_var(key, value);
    }
}

/// Locate a `.env` file: the CWD first, then beside the running executable.
fn dotenv_path() -> Option<PathBuf> {
    let mut candidates: Vec<PathBuf> = Vec::new();
    if let Ok(cwd) = std::env::current_dir() {
        candidates.push(cwd.join(".env"));
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            candidates.push(dir.join(".env"));
        }
    }
    candidates.into_iter().find(|p| p.is_file())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn arg_value_reads_a_following_token() {
        let args = vec!["bin".into(), "--surface".into(), "cli,browser".into()];
        assert_eq!(
            arg_value(&args, "--surface").as_deref(),
            Some("cli,browser")
        );
        assert_eq!(arg_value(&args, "--missing"), None);
    }}
