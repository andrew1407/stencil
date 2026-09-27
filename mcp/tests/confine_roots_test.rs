//! The roots every write is fenced into: where a relative path lands, which root a path
//! falls under, and the refusal for one that falls under none.

use std::path::{Path, PathBuf};

use stencil_mcp::confine::{confine_output_dir, Roots};

fn roots(dirs: &[&str]) -> Roots {
    Roots::new(dirs.iter().map(PathBuf::from)).expect("at least one root")
}

#[test]
fn a_relative_path_resolves_against_the_first_root() {
    let r = roots(&["/work/project", "/data"]);
    assert_eq!(r.resolve("shots/a.png"), "/work/project/shots/a.png");
    assert_eq!(r.resolve("../project/b.png"), "/work/project/b.png");
    assert_eq!(r.resolve("/data/c.png"), "/data/c.png");
    assert_eq!(r.resolve("https://example.com/a.png"), "https://example.com/a.png");
}

#[test]
fn a_path_is_placed_under_the_most_specific_root() {
    let r = roots(&["/work", "/work/project/out"]);
    let (root, full) = r.place("output", "/work/project/out/a.png").unwrap();
    assert_eq!(root, Path::new("/work/project/out"));
    assert_eq!(full, Path::new("/work/project/out/a.png"));
    let (root, _) = r.place("output", "notes/b.png").unwrap();
    assert_eq!(root, Path::new("/work"));
}

/// An absolute path elsewhere, and a relative one that climbs out, are both refused.
#[test]
fn a_path_under_no_root_is_refused_naming_the_roots() {
    let r = roots(&["/work/project"]);
    for escaping in ["/etc/passwd.png", "../../elsewhere.png", "/work/projectile/a.png"] {
        let error = r.place("output", escaping).unwrap_err();
        assert!(error.contains("outside the allowed roots (/work/project)"), "{escaping}: {error}");
    }
}

#[test]
fn no_directories_is_no_roots() {
    assert!(Roots::new(Vec::<PathBuf>::new()).is_none());
    assert!(Roots::new([PathBuf::new()]).is_none());
    assert!(Roots::cwd().primary().is_absolute());
}

/// A scrape into the root itself confines as `.`; one outside it is not run.
#[test]
fn an_output_directory_confines_relative_to_its_root() {
    let argv: Vec<std::borrow::Cow<'static, str>> =
        vec!["--source-site".into(), "http://x.test".into(), "/work/shots".into()];
    let run = confine_output_dir("/work", &argv).expect("inside the root");
    assert_eq!(run.argv[run.argv.len() - 2..], ["--confine-output", "shots"]);
    let at_root = confine_output_dir("/work/shots", &argv).expect("the root itself");
    assert_eq!(at_root.argv.last().unwrap(), ".");
    assert!(confine_output_dir("/elsewhere", &argv).is_none());
}

/// A link inside the root that leads out — or dangles — refuses the path, whether or not the
/// rest of it exists yet; a link that stays inside does not.
#[cfg(unix)]
#[test]
fn a_symbolic_link_out_of_the_root_is_refused() {
    use std::os::unix::fs::symlink;
    let root = tempfile::tempdir().unwrap();
    let away = tempfile::tempdir().unwrap();
    std::fs::create_dir(root.path().join("sub")).unwrap();
    symlink(away.path(), root.path().join("out")).unwrap();
    symlink(root.path().join("sub"), root.path().join("inner")).unwrap();
    symlink(root.path().join("missing/x.png"), root.path().join("dangling.png")).unwrap();
    let r = Roots::new([root.path().to_path_buf()]).unwrap();
    for escaping in ["out/a.png", "out/new/dir/a.png", "dangling.png"] {
        let error = r.place("output", escaping).unwrap_err();
        assert!(error.contains("through a symbolic link"), "{escaping}: {error}");
    }
    for inside in ["inner/a.png", "sub/new/a.png", "a.png"] {
        assert!(r.place("output", inside).is_ok(), "{inside}");
    }
}
