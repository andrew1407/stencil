//! CLI flag names.
//!
//! The exact option strings understood by the Zig CLI (`cli/src/args.zig`). Centralized here
//! so the flag contract is single-sourced and greppable; `build_argv` references these instead
//! of bare literals. Changing a flag string means changing it in the CLI too.

pub(super) const FLAG_SERVER: &str = "--server";
pub(super) const FLAG_INPUT: &str = "-i";
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
