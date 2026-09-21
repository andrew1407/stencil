//! The wrapper half of the CLI's `--confine-output` (`cli/src/safety/confine.zig`).
//!
//! The CLI resolves that flag against its OWN working directory, so a confined run is
//! spawned inside the sandbox root with its output relative to it; every other local path
//! in the argv is made absolute first.

use std::borrow::Cow;
use std::path::{Component, Path, PathBuf};

use crate::args::Argv;

/// The flag `cli/src/args.zig` parses.
const FLAG_CONFINE_OUTPUT: &str = "--confine-output";

/// Flags whose value is a local path the working-directory change would re-resolve.
const PATH_FLAGS: [&str; 3] = ["-i", "-l", "--script"];

/// One run rewritten for a sandbox root: spawn in `dir`, with `argv`.
pub struct Confined {
    pub dir: PathBuf,
    pub argv: Argv,
}

/// Rewrite `argv` (whose LAST token is the output path) to run inside `root`. `None` when
/// the output does not sit under `root` — the run must not happen at all.
pub fn confine(root: &str, argv: &[Cow<'static, str>]) -> Option<Confined> {
    let dir = absolute(Path::new(root));
    let (output, head) = argv.split_last()?;
    let relative = absolute(Path::new(output.as_ref()))
        .strip_prefix(&dir)
        .ok()?
        .to_path_buf();
    if relative.as_os_str().is_empty() {
        return None;
    }

    let mut out = absolutize(head);
    out.push(Cow::Borrowed(FLAG_CONFINE_OUTPUT));
    out.push(Cow::Owned(relative.to_string_lossy().into_owned()));
    Some(Confined { dir, argv: out })
}

/// Rewrite a run with NO positional output — a script, whose writes are its own `@save`
/// targets. The sandbox is the spawn directory plus the CLI's bare `--confine-output`.
pub fn confine_dir(root: &str, argv: &[Cow<'static, str>]) -> Confined {
    let mut out = absolutize(argv);
    out.push(Cow::Borrowed(FLAG_CONFINE_OUTPUT));
    Confined { dir: absolute(Path::new(root)), argv: out }
}

/// Every `PATH_FLAGS` value made absolute: the cwd change must not re-point a local path.
fn absolutize(tokens: &[Cow<'static, str>]) -> Argv {
    let mut out: Argv = Vec::with_capacity(tokens.len() + 2);
    let mut expect_path = false;
    for token in tokens {
        out.push(match expect_path {
            true => Cow::Owned(local_absolute(token)),
            false => token.clone(),
        });
        expect_path = PATH_FLAGS.contains(&token.as_ref());
    }
    out
}

/// A confined run reports its output relative to the sandbox root it ran in; join it back
/// so callers keep seeing the absolute path they asked for.
pub fn rejoin(root: Option<&str>, path: String) -> String {
    match root {
        None => path,
        Some(root) => absolute(&Path::new(root).join(path)).to_string_lossy().into_owned(),
    }
}

/// `path` against the process CWD, lexically: `.` dropped, `..` folded, no symlink
/// resolution and no existence requirement (an output file does not exist yet).
fn absolute(path: &Path) -> PathBuf {
    let joined = match path.is_absolute() {
        true => path.to_path_buf(),
        false => std::env::current_dir().unwrap_or_default().join(path),
    };
    let mut out = PathBuf::new();
    for component in joined.components() {
        match component {
            Component::CurDir => {}
            Component::ParentDir => {
                out.pop();
            }
            other => out.push(other),
        }
    }
    out
}

/// A relative local path made absolute; URLs and already-absolute paths ride unchanged.
fn local_absolute(token: &str) -> String {
    if token.contains("://") || Path::new(token).is_absolute() {
        return token.to_string();
    }
    absolute(Path::new(token)).to_string_lossy().into_owned()
}
