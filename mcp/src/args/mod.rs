//! Typed tool parameters and their translation into the CLI's argv.
//!
//! This mirrors the role of `cli/src/args.zig`: it owns the mapping between a request and
//! the exact `stencil [options] <output>` command line. The CLI fixes the pipeline order
//! and parses flags order-independently, so argv order here is only cosmetic.
//!
//! [`params`] holds the tool DTOs, [`tables`] the canonical page/colour names they are
//! checked against, [`flags`] the CLI's option strings, [`errors`] the failures, [`argv`]
//! the edit command line, and [`scrape`] the `source_site` tool end to end.

mod argv;
mod errors;
mod flags;
mod params;
mod scrape;
mod tables;

pub use argv::{build_argv, Argv};
pub use errors::EditError;
pub use params::{Blank, Crop, EditParams, LayoutArg, ProbeParams, PromptParams, SurfaceArg};
pub use scrape::{build_scrape_argv, ScrapeParams};
