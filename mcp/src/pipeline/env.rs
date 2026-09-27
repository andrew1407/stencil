//! What a spawned child keeps of this process's environment: an allowlist, so
//! `STENCIL_LLM_*`, the server tokens and any other secret here never reach a CLI run, the
//! desktop app or the OS opener.

use std::ffi::OsString;

/// A CLI child's allowlist; a run's own server tokens are added back by name.
const CHILD_ENV: [&str; 20] = [
    "PATH", "HOME", "TMPDIR", "TMP", "TEMP", "LANG", "SYSTEMROOT", "SystemRoot",
    "http_proxy", "https_proxy", "no_proxy", "all_proxy",
    "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY", "ALL_PROXY",
    "SSL_CERT_FILE", "SSL_CERT_DIR", "NO_COLOR", "DEVELOPER_DIR",
];

/// What a GUI launch needs beyond [`CHILD_ENV`]: the user, the display and session bus, and
/// the Windows profile.
const GUI_ENV: [&str; 16] = [
    "USER", "LOGNAME", "USERNAME", "SHELL", "DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY",
    "DBUS_SESSION_BUS_ADDRESS", "__CF_USER_TEXT_ENCODING",
    "WINDIR", "APPDATA", "LOCALAPPDATA", "USERPROFILE", "SYSTEMDRIVE", "COMSPEC", "PATHEXT",
];

const GUI_PREFIXES: [&str; 3] = ["LC_", "XDG_", "QT_"];

/// The allowlisted slice of `vars` for a CLI child: [`CHILD_ENV`] plus every `LC_*`.
pub fn child_env(vars: impl Iterator<Item = (OsString, OsString)>) -> Vec<(OsString, OsString)> {
    keep(vars, |name| CHILD_ENV.contains(&name) || name.starts_with("LC_"))
}

/// The allowlisted slice of `vars` for the desktop app and the OS opener. Windows spells
/// the same variable `Path` or `windir`, so names compare case-insensitively.
pub fn gui_env(vars: impl Iterator<Item = (OsString, OsString)>) -> Vec<(OsString, OsString)> {
    let listed = |name: &str| CHILD_ENV.iter().chain(&GUI_ENV).any(|k| k.eq_ignore_ascii_case(name));
    keep(vars, |name| listed(name) || GUI_PREFIXES.iter().any(|p| name.starts_with(p)))
}

fn keep(
    vars: impl Iterator<Item = (OsString, OsString)>,
    kept: impl Fn(&str) -> bool,
) -> Vec<(OsString, OsString)> {
    vars.filter(|(name, _)| name.to_str().is_some_and(&kept)).collect()
}
