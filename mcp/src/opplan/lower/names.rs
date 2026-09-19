//! The names a lowered run writes under: sanitized variant labels, the §10 save
//! destination mapped into `output_dir`, and the dedupe that keeps two runs apart.
use std::collections::HashSet;
use std::path::Path;

use super::super::MAX_LABEL_CHARS;

/// Sanitize a variant label into a `[a-z0-9-]` file stem (other runs become one dash,
/// capped at 40 chars). May come out empty — callers fall back to `variant-N`.
pub fn sanitize_label(label: &str) -> String {
    let mut out = String::new();
    for c in label.chars() {
        let c = c.to_ascii_lowercase();
        if c.is_ascii_lowercase() || c.is_ascii_digit() {
            if out.len() >= MAX_LABEL_CHARS {
                break;
            }
            out.push(c);
        } else if !out.is_empty() && !out.ends_with('-') {
            out.push('-');
        }
    }
    while out.ends_with('-') {
        out.pop();
    }
    out
}

/// Claim `stem`, suffixing `-2`, `-3`, … until it is unique among the names already taken.
pub fn dedupe(stem: String, taken: &mut HashSet<String>) -> String {
    if taken.insert(stem.clone()) {
        return stem;
    }
    let mut n = 2;
    while !taken.insert(format!("{stem}-{n}")) {
        n += 1;
    }
    format!("{stem}-{n}")
}

/// Map a §10 save destination into `output_dir`: a relative `.stencil` name as-is, any
/// other relative path as a folder. Absolute, `..` or `~` paths escape it — `None`.
pub fn honored_save_path(path: &str, output_dir: &str, stem: &str) -> Option<String> {
    use std::path::Component;
    let p = Path::new(path);
    if p.is_absolute()
        || path.starts_with('~')
        || p.components().any(|c| !matches!(c, Component::Normal(_) | Component::CurDir))
    {
        return None;
    }
    let dest = if path.to_ascii_lowercase().ends_with(".stencil") {
        Path::new(output_dir).join(p)
    } else {
        Path::new(output_dir).join(p).join(format!("{stem}.stencil"))
    };
    Some(dest.to_string_lossy().into_owned())
}

/// The file stem an unnamed §2.1 `save` derives from the turn's input (path or URL),
/// sanitized into the same `[a-z0-9-]` shape variant labels use. May come out empty.
pub fn default_save_name(input: &str) -> String {
    let tail = input.rsplit(['/', '\\']).next().unwrap_or(input);
    let stem = tail.split(['?', '#']).next().unwrap_or(tail);
    let stem = stem.rsplit_once('.').map_or(stem, |(head, _)| head);
    sanitize_label(stem)
}
