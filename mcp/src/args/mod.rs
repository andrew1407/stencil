//! Typed tool parameters and their translation into the CLI's argv.
//!
//! Mirrors `cli/src/args.zig`: it owns the mapping between a request and the exact
//! `stencil [options] <output>` command line. The CLI parses flags order-independently,
//! so argv order here is only cosmetic.

mod argv;
mod crop;
mod errors;
mod flags;
mod inspect;
mod params;
mod projects;
mod scrape;
mod script;
mod script_modes;
mod tables;

pub use argv::{build_argv, with_no_clobber, Argv};
pub use crop::{Crop, Edge};
pub use errors::EditError;
pub use inspect::{build_probe_argv, build_projects_argv};
pub use params::{
    Blank, EditParams, LayoutArg, ProbeParams, ProjectsParams, PromptParams, SurfaceArg,
};
pub use projects::{
    build_project_file_argv, build_project_files_argv, build_project_update_argv,
    check_project_id, FileKind, ProjectFileParams, ProjectUpdateParams,
};
pub use scrape::{build_scrape_argv, ScrapeParams};
pub use script::{build_script_argv, ScriptParams, MAX_SCRIPT_BYTES};
pub use script_modes::{
    build_check_argv, build_emit_argv, build_plan_argv, ScriptCheckParams, ScriptEmitParams,
    ScriptPlanParams, ScriptSource,
};
