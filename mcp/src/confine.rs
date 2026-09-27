//! The wrapper half of the CLI's `--confine-output` (`cli/src/safety/confine.zig`), and the
//! [`Roots`] every write is fenced into.
//!
//! The CLI resolves that flag against its OWN working directory, so a confined run is
//! spawned inside the sandbox root with its output relative to it.

use std::borrow::Cow;
use std::path::{Component, Path, PathBuf};

use crate::args::Argv;

/// The flag `cli/src/args.zig` parses.
pub const FLAG_CONFINE_OUTPUT: &str = "--confine-output";

/// Flags whose value is a local path the working-directory change would re-resolve.
const PATH_FLAGS: [&str; 3] = ["-i", "-l", "--script"];

/// The directories a run may write inside. The first also anchors every relative path.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Roots {
    dirs: Vec<PathBuf>,
}

impl Roots {
    /// `None` when no directory is left; each is made absolute against the process CWD.
    pub fn new(dirs: impl IntoIterator<Item = PathBuf>) -> Option<Roots> {
        let dirs: Vec<PathBuf> = dirs
            .into_iter()
            .filter(|d| !d.as_os_str().is_empty())
            .map(|d| absolute(&d))
            .collect();
        (!dirs.is_empty()).then_some(Roots { dirs })
    }

    /// The process working directory alone.
    pub fn cwd() -> Roots {
        Roots { dirs: vec![absolute(Path::new("."))] }
    }

    pub fn dirs(&self) -> &[PathBuf] {
        &self.dirs
    }

    pub fn primary(&self) -> &Path {
        &self.dirs[0]
    }

    /// A local path anchored on the primary root; a URL rides unchanged.
    pub fn resolve(&self, path: &str) -> String {
        if is_url(path) {
            return path.to_string();
        }
        absolute_from(self.primary(), Path::new(path)).to_string_lossy().into_owned()
    }

    /// The most specific root holding `path`, and `path` made absolute — or why not. A
    /// symbolic link on the way that leads out of that root refuses it as well.
    pub fn place(&self, field: &str, path: &str) -> Result<(PathBuf, PathBuf), String> {
        let full = absolute_from(self.primary(), Path::new(path));
        let root = self.dirs.iter().filter(|r| full.starts_with(r));
        match root.max_by_key(|r| r.components().count()) {
            Some(root) if !links_within(root, &full) => Err(format!(
                "`{field}` '{path}' leads out of its root ({}) through a symbolic link",
                root.display()
            )),
            Some(root) => Ok((root.clone(), full)),
            None => Err(format!(
                "`{field}` '{path}' is outside the allowed roots ({}) — write inside one of them",
                self.listing()
            )),
        }
    }

    fn listing(&self) -> String {
        let dirs: Vec<String> = self.dirs.iter().map(|d| d.display().to_string()).collect();
        dirs.join(", ")
    }
}

/// One run rewritten for a sandbox root: spawn in `dir`, with `argv`.
pub struct Confined {
    pub dir: PathBuf,
    pub argv: Argv,
}

/// Rewrite `argv` (whose LAST token is the output FILE) to run inside `root`. `None` when
/// the output does not sit under `root` — the run must not happen at all.
pub fn confine(root: &str, argv: &[Cow<'static, str>]) -> Option<Confined> {
    rewrite(root, argv, false)
}

/// The same for a positional output DIRECTORY (a scrape), which may be the root itself.
pub fn confine_output_dir(root: &str, argv: &[Cow<'static, str>]) -> Option<Confined> {
    rewrite(root, argv, true)
}

fn rewrite(root: &str, argv: &[Cow<'static, str>], dir_output: bool) -> Option<Confined> {
    let dir = absolute(Path::new(root));
    let (output, head) = argv.split_last()?;
    let mut relative = relative_to(&dir, Path::new(output.as_ref()))?;
    if relative.as_os_str().is_empty() {
        if !dir_output {
            return None;
        }
        relative = PathBuf::from(".");
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

/// `path` relative to the absolute `root`, or `None` when it does not sit under it.
pub fn relative_to(root: &Path, path: &Path) -> Option<PathBuf> {
    absolute(path).strip_prefix(root).ok().map(Path::to_path_buf)
}

/// Every `PATH_FLAGS` value made absolute: the cwd change must not re-point a local path.
pub fn absolutize(tokens: &[Cow<'static, str>]) -> Argv {
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

/// Whether `path` stays under `root` once links resolve, judged at its deepest existing
/// ancestor (where a write or a mkdir lands); a dangling link resolves nowhere.
fn links_within(root: &Path, path: &Path) -> bool {
    match (resolve_existing(root), resolve_existing(path)) {
        (Some(root), Some(path)) => path.starts_with(root),
        _ => false,
    }
}

fn resolve_existing(path: &Path) -> Option<PathBuf> {
    let mut rest = Vec::new();
    let mut at = path;
    while std::fs::symlink_metadata(at).is_err() {
        rest.push(at.file_name()?);
        at = at.parent()?;
    }
    let mut real = std::fs::canonicalize(at).ok()?;
    real.extend(rest.iter().rev());
    Some(real)
}

/// `path` against the process CWD, lexically (see [`absolute_from`]).
fn absolute(path: &Path) -> PathBuf {
    absolute_from(&std::env::current_dir().unwrap_or_default(), path)
}

/// `path` against `base`, lexically: `.` dropped, `..` folded, no symlink resolution and no
/// existence requirement (an output file does not exist yet).
fn absolute_from(base: &Path, path: &Path) -> PathBuf {
    let joined = match path.is_absolute() {
        true => path.to_path_buf(),
        false => base.join(path),
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

fn is_url(token: &str) -> bool {
    token.contains("://")
}

/// A relative local path made absolute; URLs and already-absolute paths ride unchanged.
fn local_absolute(token: &str) -> String {
    if is_url(token) || Path::new(token).is_absolute() {
        return token.to_string();
    }
    absolute(Path::new(token)).to_string_lossy().into_owned()
}
