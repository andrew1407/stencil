//! The canonical page-format and colour-name tables, and the validators that consult
//! them. Both are embedded from `browser/js/config/` — the same assets the other surfaces
//! read — because the CLI silently SKIPS a token it cannot parse.

use std::collections::HashSet;
use std::sync::LazyLock;

/// The ISO page-format names the CLI's core recognizes (`A0`–`C10`), matched
/// case-insensitively — the canonical `PAGE_SIZES` table, embedded at compile time.
static PAGE_FORMATS: LazyLock<Vec<String>> = LazyLock::new(|| {
    let constants: serde_json::Value =
        serde_json::from_str(include_str!("../../../browser/js/config/constants.json"))
            .expect("canonical browser/js/config/constants.json is not valid JSON");
    constants["PAGE_SIZES"]
        .as_object()
        .expect("browser/js/config/constants.json: PAGE_SIZES must be an object")
        .keys()
        .cloned()
        .collect()
});

/// Whether `name` is a known page-format token (case-insensitive). The CLI's `--blank`
/// parser silently SKIPS an unrecognized token (the blank would come out A4 with no
/// error), so the server must reject unknown names before they reach argv.
pub(super) fn is_page_format(name: &str) -> bool {
    PAGE_FORMATS.iter().any(|f| f.eq_ignore_ascii_case(name))
}

/// The CSS Color Module Level 4 extended colour keywords the CLI's core recognizes
/// (`parseColor` consults them after trying `transparent` and `#hex`). Only the names
/// matter here, loaded from the canonical name→hex table in
/// `browser/js/config/colorNames.json`, embedded at compile time.
static COLOR_NAMES: LazyLock<HashSet<String>> = LazyLock::new(|| {
    let table: std::collections::HashMap<String, String> =
        serde_json::from_str(include_str!("../../../browser/js/config/colorNames.json"))
            .expect("canonical browser/js/config/colorNames.json is not a valid name->hex object");
    table.into_keys().collect()
});

/// Whether `spec` is a colour the CLI's `parseColor` accepts (`core/color/colorNames.cpp`):
/// after trimming and ASCII-lowercasing, `transparent`, `#` + 3/4/6/8 hex digits, or a CSS
/// named colour. Like an unknown page token, the CLI's `--blank` parser silently skips an
/// unparseable colour, so the server must reject it before argv.
pub(super) fn is_color(spec: &str) -> bool {
    let s = spec.trim().to_ascii_lowercase();
    if s.is_empty() {
        return false;
    }
    if s == "transparent" {
        return true;
    }
    if let Some(hex) = s.strip_prefix('#') {
        return matches!(hex.len(), 3 | 4 | 6 | 8) && hex.bytes().all(|b| b.is_ascii_hexdigit());
    }
    COLOR_NAMES.contains(s.as_str())
}

#[cfg(test)]
mod canonical_tables_tests {
    use super::*;

    #[test]
    fn color_table_has_148_names_and_validates() {
        assert_eq!(COLOR_NAMES.len(), 148);
        assert!(is_color("rebeccapurple"));
        assert!(is_color(" RebeccaPurple "));
        assert!(!is_color("notacolour"));
    }

    #[test]
    fn page_format_table_has_33_names_and_validates() {
        assert_eq!(PAGE_FORMATS.len(), 33);
        assert!(is_page_format("a10"));
        assert!(is_page_format("C7"));
        assert!(!is_page_format("A11"));
        assert!(!is_page_format("Letter"));
    }
}

