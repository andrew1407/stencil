//! CLI flag names.
//!
//! The exact option strings understood by the Zig CLI (`cli/src/args.zig`). Centralized here
//! so the flag contract is single-sourced and greppable; `build_argv` references these instead
//! of bare literals. Changing a flag string means changing it in the CLI too.

pub(super) const FLAG_SERVER: &str = "--server";
pub(super) const FLAG_INPUT: &str = "-i";
pub(super) const FLAG_SCRIPT: &str = "--script";
pub(super) const FLAG_SCRIPT_CHECK: &str = "--script-check";
pub(super) const FLAG_SCRIPT_PLAN: &str = "--script-plan";
pub(super) const FLAG_SCRIPT_EMIT: &str = "--script-emit";
pub(super) const FLAG_BLANK: &str = "--blank";
pub(super) const FLAG_FRAME: &str = "-f";
pub(super) const FLAG_CROP: &str = "-c";
pub(super) const FLAG_ALBUM: &str = "--album";
pub(super) const FLAG_ROTATE: &str = "-r";
pub(super) const FLAG_LAYOUT: &str = "-l";
pub(super) const FLAG_LAYOUT_FRAME: &str = "--layout-frame";
pub(super) const FLAG_FILTER: &str = "--filter";
pub(super) const FLAG_REMOTE_UPDATE: &str = "--remote-update";
pub(super) const FLAG_REMOTE: &str = "--remote";
pub(super) const FLAG_REMOTE_NAME: &str = "--remote-name";
pub(super) const FLAG_NO_CLOBBER: &str = "--no-clobber";
pub(super) const FLAG_PROBE: &str = "--probe";
pub(super) const FLAG_LIST_PROJECTS: &str = "--list-projects";
pub(super) const FLAG_PROJECT_INFO: &str = "--project-info";
pub(super) const FLAG_PROJECT_UPDATE: &str = "--project-update";
pub(super) const FLAG_SET_NAME: &str = "--set-name";
pub(super) const FLAG_SET_DESCRIPTION: &str = "--set-description";
pub(super) const FLAG_SET_KEYWORDS: &str = "--set-keywords";
pub(super) const FLAG_SET_COLOR: &str = "--set-color";
pub(super) const FLAG_SET_BLANK_COLOR: &str = "--set-blank-color";
pub(super) const FLAG_SET_EXPIRES: &str = "--set-expires";
pub(super) const FLAG_IF_VERSION: &str = "--if-version";
pub(super) const FLAG_PROJECT_FILES: &str = "--project-files";
pub(super) const FLAG_PROJECT_FILE: &str = "--project-file";
