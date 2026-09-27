//! The image build can see every file the crate embeds: each `include_str!` that leaves `mcp/`
//! is COPYed by the server stage of `mcp/Dockerfile` and survives the repo-root `.dockerignore`.
//! CI's image job runs only once pushed; this catches a broken `docker build` in `cargo test`.

use std::fs;
use std::path::{Component, Path, PathBuf};

fn crate_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}

fn collect(dir: &Path, out: &mut Vec<PathBuf>) {
    for entry in fs::read_dir(dir).expect("readable dir").flatten() {
        let path = entry.path();
        if path.is_dir() {
            collect(&path, out);
        } else if path.extension().is_some_and(|e| e == "rs") {
            out.push(path);
        }
    }
}

/// `a/b/../c` → `a/c`, without touching the filesystem.
fn normalize(path: &Path) -> PathBuf {
    let mut out = PathBuf::new();
    for part in path.components() {
        match part {
            Component::ParentDir => {
                out.pop();
            }
            Component::CurDir => {}
            other => out.push(other),
        }
    }
    out
}

/// Every embedded file outside the crate, as a path relative to the repo root.
fn external_embeds() -> Vec<String> {
    let root = normalize(&crate_dir().join(".."));
    let mut sources = Vec::new();
    collect(&crate_dir().join("src"), &mut sources);
    let mut out = Vec::new();
    for file in sources {
        let text = fs::read_to_string(&file).expect("readable source");
        for piece in text.split("include_str!(\"").skip(1) {
            let rel = piece.split('"').next().unwrap_or_default();
            let target = normalize(&file.parent().expect("a parent").join(rel));
            if target.starts_with(crate_dir()) {
                continue;
            }
            let repo_rel = target.strip_prefix(&root).expect("inside the repo");
            out.push(repo_rel.to_string_lossy().replace('\\', "/"));
        }
    }
    out.sort();
    out.dedup();
    out
}

/// The COPY sources of the stage that runs `cargo build` (`FROM … AS server`).
fn server_stage_copies() -> Vec<String> {
    let dockerfile = fs::read_to_string(crate_dir().join("Dockerfile")).expect("mcp/Dockerfile");
    let stage = dockerfile.split("\nFROM ").find(|s| s.contains(" AS server")).expect("a server stage");
    stage
        .lines()
        .filter_map(|line| line.trim().strip_prefix("COPY "))
        .filter(|args| !args.starts_with("--from"))
        .filter_map(|args| args.split_whitespace().next())
        .map(|src| src.trim_end_matches('/').to_string())
        .collect()
}

/// One `.dockerignore` pattern against a path: `**` spans segments, `*` stays inside one.
fn glob(pattern: &[&str], path: &[&str]) -> bool {
    match (pattern.first(), path.first()) {
        (None, None) => true,
        (Some(&"**"), _) => glob(&pattern[1..], path) || (!path.is_empty() && glob(pattern, &path[1..])),
        (Some(p), Some(s)) => segment(p.as_bytes(), s.as_bytes()) && glob(&pattern[1..], &path[1..]),
        _ => false,
    }
}

fn segment(p: &[u8], s: &[u8]) -> bool {
    match (p.first(), s.first()) {
        (None, None) => true,
        (Some(b'*'), _) => segment(&p[1..], s) || (!s.is_empty() && segment(p, &s[1..])),
        (Some(a), Some(b)) => a == b && segment(&p[1..], &s[1..]),
        _ => false,
    }
}

/// Docker's rule: the last matching line wins, and a pattern that names a parent excludes it all.
fn ignored(dockerignore: &str, path: &str) -> bool {
    let segs: Vec<&str> = path.split('/').collect();
    let mut out = false;
    for line in dockerignore.lines().map(str::trim).filter(|l| !l.is_empty() && !l.starts_with('#')) {
        let (negated, pattern) = match line.strip_prefix('!') {
            Some(rest) => (true, rest),
            None => (false, line),
        };
        let pat: Vec<&str> = pattern.trim_matches('/').split('/').collect();
        if (1..=segs.len()).any(|n| glob(&pat, &segs[..n])) {
            out = !negated;
        }
    }
    out
}

#[test]
fn every_external_embed_reaches_the_server_stage() {
    let embeds = external_embeds();
    assert!(embeds.iter().any(|e| e.ends_with("opRegistry.json")), "the scan found the registry embed");
    let copies = server_stage_copies();
    let dockerignore = fs::read_to_string(crate_dir().join("../.dockerignore")).expect(".dockerignore");
    for embed in &embeds {
        assert!(
            copies.iter().any(|c| embed.starts_with(&format!("{c}/"))),
            "mcp/Dockerfile's server stage copies no directory holding {embed} (COPY sources: {copies:?})"
        );
        assert!(!ignored(&dockerignore, embed), ".dockerignore drops {embed} from the build context");
    }
}

#[test]
fn the_ignore_matcher_reads_docker_rules() {
    let rules = "# docs\n**/*.md\n.claude\n!contracts/stc/stc-contract.md\ndesktop\n";
    assert!(ignored(rules, "mcp/README.md"));
    assert!(ignored(rules, "README.md"));
    assert!(ignored(rules, ".claude/rules/tests.md"));
    assert!(ignored(rules, "desktop/src/main.cpp"));
    assert!(!ignored(rules, "contracts/stc/stc-contract.md"));
    assert!(!ignored(rules, "browser/js/config/constants.json"));
}
