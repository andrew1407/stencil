//! The `stencil_edit` command line: the push helper, the normalized [`Source`], and the
//! flat flag mapping that is `build_argv`.

use std::borrow::Cow;

use super::errors::EditError;
use super::flags::*;
use super::params::{Blank, EditParams};
use super::tables::{is_color, is_page_format};

/// One CLI command line: the flag tokens ride borrowed, only values allocate.
pub type Argv = Vec<Cow<'static, str>>;

pub(super) struct ArgvBuilder {
    argv: Argv,
}

impl ArgvBuilder {
    pub(super) fn new() -> Self {
        Self { argv: Vec::new() }
    }

    /// Push a flag and its value as two argv tokens.
    pub(super) fn opt(&mut self, flag: &'static str, value: impl Into<Cow<'static, str>>) {
        self.argv.push(Cow::Borrowed(flag));
        self.argv.push(value.into());
    }

    /// Push a bare flag (no value).
    pub(super) fn switch(&mut self, flag: &'static str) {
        self.argv.push(Cow::Borrowed(flag));
    }

    /// Push a bare flag only when `cond` holds.
    pub(super) fn switch_if(&mut self, flag: &'static str, cond: bool) {
        if cond {
            self.switch(flag);
        }
    }

    /// Push a bare positional/value token (no flag).
    pub(super) fn arg(&mut self, value: impl Into<Cow<'static, str>>) {
        self.argv.push(value.into());
    }

    pub(super) fn into_argv(self) -> Argv {
        self.argv
    }
}

/// The validated, normalized source of an edit: exactly one of a plain input, a blank
/// canvas, or a named project fetched from a collaboration server. `TryFrom<&EditParams>`
/// is the single normalization boundary — it runs the source/output/server/remote guards
/// once, in the CLI's own order, so the flat public `EditParams` (and its JSON schema) stay
/// untouched while `build_argv` consumes a shape that can't be inconsistent.
enum Source<'a> {
    Input(&'a str),
    Blank(&'a Blank),
    ServerProject { server: &'a str, name: &'a str },
}

impl<'a> TryFrom<&'a EditParams> for Source<'a> {
    type Error = EditError;

    fn try_from(p: &'a EditParams) -> Result<Self, EditError> {
        if p.input.is_some() && p.blank.is_some() {
            return Err(EditError::SourceConflict);
        }
        if p.input.is_none() && p.blank.is_none() {
            return Err(EditError::NoSource);
        }

        // Flag-injection guard on the positional `output`: the CLI has *no* `--`
        // end-of-options terminator, so a dash-leading `output` would parse as a flag (`-l`
        // would even swallow the next token). Mirrors the CLI's own `arg[0] == '-'` test.
        // Scheme/host SSRF filtering for `input`/`server`/`remote` is deliberately NOT done
        // here — it is enforced downstream in the CLI; this builder only guarantees each
        // value rides as one inert argv token (no shell, no splitting).
        if p.output.trim().is_empty() {
            return Err(EditError::EmptyValue("output"));
        }
        if p.output.starts_with('-') {
            return Err(EditError::DashValue("output", p.output.clone()));
        }

        // Collaboration-server invariants, mirroring the CLI's own checks.
        if p.server.is_some() {
            if p.blank.is_some() {
                return Err(EditError::ServerWithBlank);
            }
            if p.input.is_none() {
                return Err(EditError::ServerNeedsInput);
            }
        }
        if p.remote_update.unwrap_or(false) && p.server.is_none() {
            return Err(EditError::RemoteUpdateWithoutServer);
        }
        if p.remote_name.is_some() && p.remote.is_none() {
            return Err(EditError::RemoteNameWithoutRemote);
        }

        // Normalize. The guards above guarantee `input` is Some whenever `server` is.
        Ok(if let Some(server) = p.server.as_deref() {
            Source::ServerProject {
                server,
                name: p.input.as_deref().expect("server implies input"),
            }
        } else if let Some(input) = p.input.as_deref() {
            Source::Input(input)
        } else {
            Source::Blank(p.blank.as_ref().expect("no input implies blank"))
        })
    }
}

impl Source<'_> {
    /// Emit the source's leading argv: `--server <url> -i <name>`, `-i <input>`, or the
    /// `--blank …` series. `--server <url>` conceptually precedes `-i` (it changes what
    /// `-i` means), though the CLI parses order-independently.
    fn push_argv(&self, b: &mut ArgvBuilder) -> Result<(), EditError> {
        match self {
            Source::Input(input) => b.opt(FLAG_INPUT, input.to_string()),
            Source::ServerProject { server, name } => {
                b.opt(FLAG_SERVER, server.to_string());
                b.opt(FLAG_INPUT, name.to_string());
            }
            Source::Blank(blank) => blank.push_argv(b)?,
        }
        Ok(())
    }
}

impl Blank {
    /// Emit `--blank [page] [w h] [color]`, validating the traps the CLI would otherwise
    /// swallow silently (an unknown page token or unparseable colour is skipped by the
    /// CLI's `--blank` parser, so it must be rejected here instead of yielding a default).
    fn push_argv(&self, b: &mut ArgvBuilder) -> Result<(), EditError> {
        b.switch(FLAG_BLANK);
        if let Some(page) = &self.page {
            // Mirrors the CLI's own rule: a format token and explicit dims can't combine.
            if self.width.is_some() || self.height.is_some() {
                return Err(EditError::BlankPageAndDims);
            }
            if !is_page_format(page) {
                return Err(EditError::UnknownPageFormat(page.clone()));
            }
            b.arg(page.clone());
        }
        match (self.width, self.height) {
            (Some(w), Some(h)) => {
                b.arg(w.to_string());
                b.arg(h.to_string());
            }
            (None, None) => {}
            _ => return Err(EditError::BlankHalfDims),
        }
        if let Some(color) = &self.color {
            if !is_color(color) {
                return Err(EditError::UnknownColor(color.clone()));
            }
            b.arg(color.clone());
        }
        Ok(())
    }
}

/// Build the `stencil` argv from edit parameters. `layout_path` is the already-resolved
/// path passed to `-l` (a temp file for an inline layout, or the user's path/URL); pass
/// `None` to omit `--layout`. All validation happens in `Source::try_from`, so the body is a
/// flat, order-fixed mapping of the remaining flags.
pub fn build_argv(
    params: &EditParams,
    layout_path: Option<&str>,
) -> Result<Argv, EditError> {
    let source = Source::try_from(params)?;

    let mut b = ArgvBuilder::new();
    source.push_argv(&mut b)?;

    if let Some(frame) = params.frame {
        b.opt(FLAG_FRAME, frame.to_string());
    }
    if let Some(crop) = &params.crop {
        let spec = crop.to_spec();
        if !spec.is_empty() {
            b.opt(FLAG_CROP, spec);
        }
    }
    b.switch_if(FLAG_ALBUM, params.album.unwrap_or(false));
    if let Some(rotate) = params.rotate {
        b.opt(FLAG_ROTATE, rotate.to_string());
    }
    if let Some(path) = layout_path {
        b.opt(FLAG_LAYOUT, path.to_string());
        // Only meaningful alongside a layout; omitted = the CLI's `current` default.
        if let Some(frame) = &params.layout_frame {
            b.opt(FLAG_LAYOUT_FRAME, frame.clone());
        }
    }
    if let Some(filter) = &params.filter {
        b.opt(FLAG_FILTER, filter.clone());
    }

    // Server delivery: write the result back into the fetched project, and/or push it as a
    // new project. The result is always saved locally too (the positional output below).
    b.switch_if(FLAG_REMOTE_UPDATE, params.remote_update.unwrap_or(false));
    if let Some(remote) = &params.remote {
        b.opt(FLAG_REMOTE, remote.clone());
    }
    if let Some(name) = &params.remote_name {
        b.opt(FLAG_REMOTE_NAME, name.clone());
    }

    b.arg(params.output.clone());
    Ok(b.into_argv())
}
