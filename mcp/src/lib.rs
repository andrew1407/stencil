//! Stencil MCP server library.
//!
//! A thin, typed adapter that exposes Stencil's image/video editing pipeline as Model
//! Context Protocol tools. It shells out to the headless Zig CLI (`cli/`), which runs the
//! shared C++ `core/`, so results match the browser, desktop, and CLI front-ends. The
//! server itself holds no image logic — it only maps tool parameters to CLI argv and parses
//! the CLI's output back into structured results.

pub mod args;
pub mod config;
pub mod deliver;
pub mod layout;
pub mod locate;
pub mod outcome;
pub mod pipeline;
pub mod server;
