//! The CLI's reporting modes (`cli/CONTRACT.md` §6): `--probe` and the project reads, each
//! one JSON document on stdout with nothing written.

use super::argv::{Argv, ArgvBuilder};
use super::errors::EditError;
use super::flags::{FLAG_INPUT, FLAG_LIST_PROJECTS, FLAG_PROBE, FLAG_PROJECT_INFO, FLAG_SERVER};
use super::script::dash_free;

/// `stencil --probe -i <input>`.
pub fn build_probe_argv(input: &str) -> Result<Argv, EditError> {
    dash_free("input", input)?;
    let mut b = ArgvBuilder::new();
    b.switch(FLAG_PROBE);
    b.opt(FLAG_INPUT, input.to_string());
    Ok(b.into_argv())
}

/// `stencil --server <origin> --list-projects`, or `--project-info <id>` for one project.
pub fn build_projects_argv(origin: &str, id: Option<&str>) -> Result<Argv, EditError> {
    dash_free("server", origin)?;
    let mut b = ArgvBuilder::new();
    b.opt(FLAG_SERVER, origin.to_string());
    match id {
        Some(id) => {
            dash_free("id", id)?;
            b.opt(FLAG_PROJECT_INFO, id.to_string());
        }
        None => b.switch(FLAG_LIST_PROJECTS),
    }
    Ok(b.into_argv())
}
