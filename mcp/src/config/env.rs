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
/// the bot's default): a hung CLI must never pin an MCP tool call forever.
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
    apply_dotenv(&contents);
}

/// Set every pair the process env does not already carry. `setenv` races another thread's
/// `getenv`, so this runs on the one thread there is, before the runtime starts.
fn apply_dotenv(contents: &str) {
    for (key, value) in parse_dotenv(contents) {
        if std::env::var_os(&key).is_none() {
            std::env::set_var(key, value);
        }
    }
}

/// The `KEY=VALUE` pairs of a dotenv file in file order: blanks and `#` comments skipped, a
/// leading `export ` dropped, and one pair of matching surrounding quotes stripped.
fn parse_dotenv(contents: &str) -> Vec<(String, String)> {
    let mut pairs = Vec::new();
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
        if key.is_empty() {
            continue;
        }
        let value = value.trim();
        let value = value
            .strip_prefix('"')
            .and_then(|v| v.strip_suffix('"'))
            .or_else(|| value.strip_prefix('\'').and_then(|v| v.strip_suffix('\'')))
            .unwrap_or(value);
        pairs.push((key.to_string(), value.to_string()));
    }
    pairs
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
    }

    #[test]
    fn dotenv_reads_exports_quotes_and_comments() {
        let pairs = parse_dotenv(
            "# a comment\n\nexport STENCIL_LLM_MODEL=\"llava:7b\"\n\
             STENCIL_LLM_BASE_URL = 'http://host:11434/'\nBARE=x=y\n=novalue\nnokey\n",
        );
        assert_eq!(
            pairs,
            vec![
                ("STENCIL_LLM_MODEL".to_string(), "llava:7b".to_string()),
                ("STENCIL_LLM_BASE_URL".to_string(), "http://host:11434/".to_string()),
                ("BARE".to_string(), "x=y".to_string()),
            ]
        );
    }

    /// The file fills gaps; the process env always wins.
    #[test]
    fn dotenv_never_overrides_the_process_env() {
        std::env::set_var("STENCIL_TEST_DOTENV_PRESET", "from-the-env");
        apply_dotenv(
            "STENCIL_TEST_DOTENV_PRESET=from-the-file\nSTENCIL_TEST_DOTENV_FRESH=from-the-file\n",
        );
        assert_eq!(std::env::var("STENCIL_TEST_DOTENV_PRESET").unwrap(), "from-the-env");
        assert_eq!(std::env::var("STENCIL_TEST_DOTENV_FRESH").unwrap(), "from-the-file");
        std::env::remove_var("STENCIL_TEST_DOTENV_PRESET");
        std::env::remove_var("STENCIL_TEST_DOTENV_FRESH");
    }
}
