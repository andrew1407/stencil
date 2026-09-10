//! The size + comment ratchet for this crate's Rust sources.
//!
//! `size_budget.json` records the lines each listed file may keep and the comment share each
//! directory may keep: nothing may grow, a NEW file over `maxNewFileLines` fails until it is
//! split or recorded, and a file that shrank prints a note so its number can be lowered.
//! Rust sources carry inline `#[cfg(test)] mod tests` blocks, so an entry records both the
//! whole file (`lines`) and the part before that module (`prod`); the limit applies to `prod`.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use serde_json::Value;

/// Total / production / comment lines of one file.
#[derive(Clone, Copy, Default)]
struct Counts {
    total: usize,
    prod: usize,
    comments: usize,
}

/// Scanner state: 0 = code, 1 = normal string, 2 + n = raw string closed by `"` + n hashes.
const CODE: usize = 0;
const NORMAL: usize = 1;
const RAW: usize = 2;

/// Count a file's lines. Tracks block-comment nesting and string literals (normal, raw,
/// byte) so a `//` or `/*` inside a literal is never counted as a comment.
fn measure(src: &str) -> Counts {
    let mut c = Counts::default();
    let (mut depth, mut state) = (0usize, CODE);
    let mut prod_end = None;
    for line in src.lines() {
        c.total += 1;
        let head = line.trim_start();
        if depth > 0 || (state == CODE && (head.starts_with("//") || head.starts_with("/*"))) {
            c.comments += 1;
        }
        if prod_end.is_none() && depth == 0 && state == CODE && line.starts_with("#[cfg(test)]") {
            prod_end = Some(c.total - 1);
        }
        scan(line, &mut depth, &mut state);
    }
    c.prod = prod_end.unwrap_or(c.total);
    c
}

/// Carry the block-comment / string state across one line.
fn scan(line: &str, depth: &mut usize, state: &mut usize) {
    let b = line.as_bytes();
    let mut i = 0;
    while i < b.len() {
        if *depth > 0 {
            let (open, close) = (b[i..].starts_with(b"/*"), b[i..].starts_with(b"*/"));
            *depth += usize::from(open);
            *depth -= usize::from(close);
            i += if open || close { 2 } else { 1 };
        } else if *state >= RAW {
            let hashes = *state - RAW;
            let closes = b[i] == b'"' && b[i + 1..].iter().take(hashes).filter(|&&x| x == b'#').count() == hashes;
            *state = if closes { CODE } else { *state };
            i += if closes { 1 + hashes } else { 1 };
        } else if *state == NORMAL {
            *state = if b[i] == b'"' { CODE } else { NORMAL };
            i += if b[i] == b'\\' { 2 } else { 1 };
        } else if b[i..].starts_with(b"//") {
            return;
        } else if b[i..].starts_with(b"/*") {
            *depth = 1;
            i += 2;
        } else if let Some((hashes, len)) = raw_start(b, i) {
            *state = RAW + hashes;
            i += len;
        } else if b[i] == b'"' {
            *state = NORMAL;
            i += 1;
        } else {
            i += if b[i] == b'\'' { char_len(b, i) } else { 1 };
        }
    }
}

/// If a raw string opens at `i` (`r"`, `r#"`, `br##"`, …), its hash count and prefix length.
fn raw_start(b: &[u8], i: usize) -> Option<(usize, usize)> {
    let after_ident = i > 0 && (b[i - 1].is_ascii_alphanumeric() || b[i - 1] == b'_');
    if !matches!(b[i], b'r' | b'b') || after_ident {
        return None;
    }
    let mut j = i + usize::from(b[i] == b'b');
    if b.get(j) != Some(&b'r') {
        return None;
    }
    j += 1;
    let first_hash = j;
    while b.get(j) == Some(&b'#') {
        j += 1;
    }
    (b.get(j) == Some(&b'"')).then(|| (j - first_hash, j + 1 - i))
}

/// The length of a char literal at `i` (`'x'`, `'\n'`); anything longer is a lifetime.
fn char_len(b: &[u8], i: usize) -> usize {
    let end = if b.get(i + 1) == Some(&b'\\') { i + 3 } else { i + 2 };
    if b.get(end) == Some(&b'\'') {
        end + 1 - i
    } else {
        1
    }
}

/// Walk up from this test file for the repo root (the ancestor holding `cli/build.zig`).
fn repo_root() -> PathBuf {
    let mut dir = Path::new(concat!(env!("CARGO_MANIFEST_DIR"), "/tests"));
    loop {
        if dir.join("cli/build.zig").is_file() {
            return dir.to_path_buf();
        }
        dir = dir.parent().expect("no repo root above the test file");
    }
}

fn budget(root: &Path) -> Value {
    let path = root.join("mcp/tests/size_budget.json");
    let raw = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", path.display()));
    serde_json::from_str(&raw).unwrap_or_else(|e| panic!("size_budget.json is not valid JSON: {e}"))
}

/// An entry is a bare line count, or `{"lines":…, "prod":…}` for a file with inline tests.
fn recorded(name: &str, entry: &Value) -> (usize, usize) {
    if let Some(n) = entry.as_u64() {
        return (n as usize, n as usize);
    }
    let total = entry["lines"]
        .as_u64()
        .unwrap_or_else(|| panic!("{name}: an entry needs a number or a `lines` field"));
    (total as usize, entry["prod"].as_u64().unwrap_or(total) as usize)
}

/// Measure every `.rs` file in scope, keyed by its repo-relative path.
fn survey(root: &Path) -> BTreeMap<String, Counts> {
    let mut out = BTreeMap::new();
    for dir in ["mcp/src", "mcp/tests"] {
        collect(root, &root.join(dir), &mut out);
    }
    out
}

fn collect(root: &Path, dir: &Path, out: &mut BTreeMap<String, Counts>) {
    let entries =
        std::fs::read_dir(dir).unwrap_or_else(|e| panic!("cannot read {}: {e}", dir.display()));
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            collect(root, &path, out);
        } else if path.extension().is_some_and(|e| e == "rs") {
            let src = std::fs::read_to_string(&path).expect("a readable source file");
            let name = path.strip_prefix(root).expect("a path under the repo root");
            out.insert(name.to_string_lossy().replace('\\', "/"), measure(&src));
        }
    }
}

#[test]
fn no_rust_file_grows_past_its_recorded_budget() {
    let root = repo_root();
    let budget = budget(&root);
    let max = budget["maxNewFileLines"].as_u64().expect("maxNewFileLines") as usize;
    let files = budget["files"].as_object().expect("files");
    let exceptions = budget["exceptions"].as_object().expect("exceptions");
    let mut problems = Vec::new();
    for (name, c) in survey(&root) {
        if exceptions.contains_key(&name) {
            continue;
        }
        let Some(entry) = files.get(&name) else {
            if c.prod > max {
                problems.push(format!("{name}: a new file of {} production lines is over maxNewFileLines {max}", c.prod));
            }
            continue;
        };
        let (total, prod) = recorded(&name, entry);
        if c.total > total {
            problems.push(format!("{name}: {} lines, budget {total}", c.total));
        }
        if c.prod > prod {
            problems.push(format!("{name}: {} production lines, budget {prod}", c.prod));
        }
        if c.total * 10 < total * 9 {
            println!("note: {name} is down to {} lines (budget {total}) — ratchet it down", c.total);
        }
    }
    for name in files.keys().chain(exceptions.keys()) {
        if !root.join(name).is_file() {
            problems.push(format!("{name}: listed in the budget but no longer exists"));
        }
    }
    assert!(problems.is_empty(), "size budget exceeded:\n  {}", problems.join("\n  "));
}

#[test]
fn comment_share_per_directory_does_not_rise() {
    let root = repo_root();
    let recorded = budget(&root)["commentPct"].as_object().cloned().expect("commentPct");
    let mut dirs: BTreeMap<String, (usize, usize)> = BTreeMap::new();
    for (name, c) in survey(&root) {
        let (dir, _) = name.rsplit_once('/').expect("a file inside a scoped directory");
        let tally = dirs.entry(dir.to_string()).or_default();
        tally.0 += c.comments;
        tally.1 += c.total;
    }
    let mut problems = Vec::new();
    for (dir, (comments, total)) in &dirs {
        let pct = comments * 100 / total;
        match recorded.get(dir).and_then(Value::as_u64).map(|n| n as usize) {
            Some(budget) if pct > budget => {
                problems.push(format!("{dir}: comments are {pct}% of lines, budget {budget}%"));
            }
            Some(budget) if pct < budget => {
                println!("note: {dir} is down to {pct}% comments (budget {budget}%) — ratchet it down");
            }
            None => problems.push(format!("{dir}: not listed in commentPct (currently {pct}%)")),
            _ => {}
        }
    }
    assert!(problems.is_empty(), "comment budget exceeded:\n  {}", problems.join("\n  "));
}
