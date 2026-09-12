//! Import-direction lint for mcp/src. `.claude/rules/architecture.md` lists the layers top-down
//! — `server/` + tools → `opplan/` → `args` → `pipeline` → `llm` — so a module may reach every
//! module BELOW it in that row and none above. Today's crossings are a frozen allowance.

use std::collections::BTreeSet;
use std::fs;
use std::path::{Path, PathBuf};

const ORDER: [&str; 5] = ["llm", "pipeline", "args", "opplan", "server"];

/// Shrink this list; never add to it. Each entry is "file → module" as the scan reports it.
const ALLOWANCE: [(&str, &str); 2] = [
    ("pipeline/mod.rs → args", "the runner takes EditParams/ScrapeParams; args sits above it in the row"),
    ("pipeline/run.rs → args", "same seam: run() builds argv from args::EditParams"),
];

fn rank(module: &str) -> Option<usize> {
    ORDER.iter().position(|m| *m == module)
}

fn collect(dir: &Path, out: &mut Vec<PathBuf>) {
    let mut entries: Vec<_> = fs::read_dir(dir).expect("readable dir").flatten().collect();
    entries.sort_by_key(|e| e.path());
    for entry in entries {
        let path = entry.path();
        if path.is_dir() {
            collect(&path, out);
        } else if path.extension().is_some_and(|e| e == "rs") {
            out.push(path);
        }
    }
}

/// The top-level module that owns `rel` (`args/mod.rs` and `confine.rs` → `args`, `confine`);
/// `lib.rs` and `main.rs` are the crate root and own nothing.
fn owner(rel: &str) -> Option<String> {
    let first = rel.split('/').next()?;
    let name = first.strip_suffix(".rs").unwrap_or(first);
    if rel.contains('/') || !matches!(name, "lib" | "main") { Some(name.to_string()) } else { None }
}

/// Every module named after `crate::` (or `super::` in a file directly under src/, where super
/// IS the crate) on one code line, including the `crate::{a, b}` brace form.
fn referenced(line: &str, top_level_file: bool) -> Vec<String> {
    let code = line.split("//").next().unwrap_or("");
    let mut out = Vec::new();
    let mut prefixes = vec!["crate::"];
    if top_level_file {
        prefixes.push("super::");
    }
    for prefix in prefixes {
        for (i, _) in code.match_indices(prefix) {
            let rest = &code[i + prefix.len()..];
            if let Some(inner) = rest.strip_prefix('{') {
                let group = inner.split('}').next().unwrap_or("");
                out.extend(group.split(',').filter_map(|s| ident(s.trim())));
            } else if let Some(name) = ident(rest) {
                out.push(name);
            }
        }
    }
    out
}

fn ident(s: &str) -> Option<String> {
    let name: String = s.chars().take_while(|c| c.is_alphanumeric() || *c == '_').collect();
    if name.is_empty() || name == "self" { None } else { Some(name) }
}

/// Every cross-module reference in `src` as `(from_file, from_module, to_module)`.
fn scan(src: &Path) -> Vec<(String, String, String)> {
    let mut files = Vec::new();
    collect(src, &mut files);
    let mut edges = Vec::new();
    for path in files {
        let rel = path.strip_prefix(src).unwrap().to_string_lossy().replace('\\', "/");
        let Some(from) = owner(&rel) else { continue };
        let top_level_file = !rel.contains('/');
        let text = fs::read_to_string(&path).expect("readable source");
        let mut seen = BTreeSet::new();
        for line in text.lines() {
            for to in referenced(line, top_level_file) {
                if to != from && seen.insert(to.clone()) {
                    edges.push((rel.clone(), from.clone(), to));
                }
            }
        }
    }
    edges
}

/// `(rightward, unranked)`: edges that climb the row, and module names outside it.
fn violations(edges: &[(String, String, String)]) -> (Vec<String>, BTreeSet<String>) {
    let mut rightward = Vec::new();
    let mut unranked = BTreeSet::new();
    for (file, from, to) in edges {
        match (rank(from), rank(to)) {
            (Some(a), Some(b)) if b > a => rightward.push(format!("{file} → {to}")),
            (Some(_), Some(_)) => {}
            (a, b) => {
                if a.is_none() { unranked.insert(from.clone()); }
                if b.is_none() { unranked.insert(to.clone()); }
            }
        }
    }
    (rightward, unranked)
}

fn src_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("src")
}

#[test]
fn the_scan_saw_the_whole_tree() {
    let edges = scan(&src_dir());
    assert!(edges.len() > 30, "cross-module references were found: {}", edges.len());
    let has = |f: &str, t: &str| edges.iter().any(|(_, a, b)| a == f && b == t);
    assert!(has("server", "opplan"), "server → opplan is indexed");
    assert!(has("opplan", "args"), "opplan → args is indexed");
    assert!(has("server", "pipeline"), "the crate::{{a, b}} form is indexed");
}

#[test]
fn no_module_reaches_a_layer_above_it() {
    let (rightward, _) = violations(&scan(&src_dir()));
    let unlisted: Vec<_> =
        rightward.iter().filter(|e| !ALLOWANCE.iter().any(|(k, _)| k == e)).collect();
    assert!(unlisted.is_empty(), "new upward references; move the code down instead:\n  {unlisted:?}");
}

#[test]
fn every_allowance_is_still_needed() {
    let (rightward, _) = violations(&scan(&src_dir()));
    let stale: Vec<_> =
        ALLOWANCE.iter().filter(|(k, _)| !rightward.iter().any(|e| e == k)).collect();
    assert!(stale.is_empty(), "the violation is gone; delete its allowance: {stale:?}");
}

#[test]
fn modules_outside_the_documented_row_are_the_known_helpers() {
    let (_, unranked) = violations(&scan(&src_dir()));
    let known: BTreeSet<String> = [
        "config", "confine", "deliver", "imagesize", "layout", "llmtransport", "locate",
        "outcome", "registry",
    ]
    .into_iter()
    .map(String::from)
    .collect();
    assert_eq!(unranked, known, "a module joined or left the tree; place it in the row");
}

#[test]
fn an_injected_upward_reference_is_caught() {
    let root = std::env::temp_dir().join(format!("stencil-layers-{}", std::process::id()));
    let _ = fs::remove_dir_all(&root);
    for dir in ["args", "pipeline", "llm", "server"] {
        fs::create_dir_all(root.join(dir)).unwrap();
    }
    fs::write(root.join("lib.rs"), "pub mod args;\npub mod pipeline;\n").unwrap();
    fs::write(root.join("args/mod.rs"), "use crate::pipeline::Run; // crate::server\npub fn a() {}\n").unwrap();
    fs::write(root.join("pipeline/mod.rs"), "use crate::llm::f;\npub struct Run;\n").unwrap();
    fs::write(root.join("llm/mod.rs"), "use crate::server::S;\npub fn f() -> S { S }\n").unwrap();
    fs::write(root.join("server/mod.rs"), "use crate::{args, llm};\npub struct S;\n").unwrap();
    fs::write(root.join("locate.rs"), "use super::server::S;\n").unwrap();
    let (rightward, unranked) = violations(&scan(&root));
    fs::remove_dir_all(&root).unwrap();
    assert_eq!(rightward, ["llm/mod.rs → server"]);
    assert_eq!(unranked.into_iter().collect::<Vec<_>>(), ["locate"]);
}
