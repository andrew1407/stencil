//! Typed tool parameters and their translation into the CLI's argv.
//!
//! Mirrors `cli/src/args.zig`: it owns the mapping between a request and the exact
//! `stencil [options] <output>` command line. The CLI parses flags order-independently,
//! so argv order here is only cosmetic.

mod argv;
mod errors;
mod flags;
mod params;
mod scrape;
mod script;
mod tables;

pub use argv::{build_argv, Argv};
pub use errors::EditError;
pub use params::{Blank, Crop, EditParams, LayoutArg, ProbeParams, PromptParams, SurfaceArg};
pub use scrape::{build_scrape_argv, ScrapeParams};
pub use script::{build_script_argv, ScriptParams, MAX_SCRIPT_BYTES};
