//! Hand-written matchers for `regexes` (flag-free sources, one per grammar).
//!
//! This crate has no regex dependency, so every grammar the registry names is matched by
//! hand here — one self-contained function per `regexes` source, its pattern in the doc.

/// `^-?(\d+(\.\d+)?|\.\d+)(%|px|cm|in)?$`
fn is_crop_token(token: &str) -> bool {
    let rest = token.strip_prefix('-').unwrap_or(token);
    let unit_start = rest
        .find(|c: char| !c.is_ascii_digit() && c != '.')
        .unwrap_or(rest.len());
    let (number, unit) = rest.split_at(unit_start);
    let digits = |s: &str| s.bytes().all(|b| b.is_ascii_digit());
    let valid_number = match number.split_once('.') {
        None => !number.is_empty() && digits(number),
        Some((int, frac)) => !frac.is_empty() && digits(frac) && digits(int),
    };
    valid_number && matches!(unit, "" | "%" | "px" | "cm" | "in")
}

/// `^0*[1-9]\d*:0*[1-9]\d*$` — both sides > 0.
fn is_crop_aspect(token: &str) -> bool {
    let positive = |s: &str| {
        !s.is_empty() && s.bytes().all(|b| b.is_ascii_digit()) && s.bytes().any(|b| b != b'0')
    };
    token
        .split_once(':')
        .is_some_and(|(w, h)| positive(w) && positive(h))
}

/// `^[abc](10|[0-9])$`
fn is_page_format(format: &str) -> bool {
    let mut chars = format.chars();
    let Some(series) = chars.next() else {
        return false;
    };
    let number = chars.as_str();
    matches!(series, 'a' | 'b' | 'c')
        && (number == "10" || (number.len() == 1 && number.bytes().all(|b| b.is_ascii_digit())))
}

/// `^#[0-9a-fA-F]{6}$`
fn is_hex(color: &str) -> bool {
    color
        .strip_prefix('#')
        .is_some_and(|hex| hex.len() == 6 && hex.bytes().all(|b| b.is_ascii_hexdigit()))
}

/// `^[0-9<var>+\-*/(). ]+$`
fn is_formula(expr: &str, var: char) -> bool {
    !expr.is_empty()
        && expr
            .chars()
            .all(|c| c.is_ascii_digit() || "+-*/(). ".contains(c) || c == var)
}

/// JS `\s` (no `u` flag): Unicode White_Space plus U+FEFF, minus U+0085.
fn is_js_space(c: char) -> bool {
    c == '\u{feff}' || (c.is_whitespace() && c != '\u{85}')
}

/// `^[hH][tT][tT][pP][sS]?://\S+$`
fn is_http_url(s: &str) -> bool {
    let lower: String = s.chars().take(8).collect::<String>().to_ascii_lowercase();
    let rest = if lower.starts_with("https://") {
        &s[8..]
    } else if lower.starts_with("http://") {
        &s[7..]
    } else {
        return false;
    };
    !rest.is_empty() && !rest.chars().any(is_js_space)
}

/// `^[a-zA-Z][a-zA-Z0-9+.\-]*://` — a prefix match.
fn has_url_scheme(s: &str) -> bool {
    let Some(pos) = s.find("://") else {
        return false;
    };
    let mut chars = s[..pos].chars();
    matches!(chars.next(), Some(c) if c.is_ascii_alphabetic())
        && chars.all(|c| c.is_ascii_alphanumeric() || matches!(c, '+' | '.' | '-'))
}

/// Match `s` against the registry grammar `name`.
pub fn matches(name: &str, s: &str) -> bool {
    match name {
        "CROP_TOKEN" => is_crop_token(s),
        "CROP_ASPECT" => is_crop_aspect(s),
        "PAGE_FORMAT" => is_page_format(s),
        "HEX" => is_hex(s),
        "CSS_NAME" => !s.is_empty() && s.bytes().all(|b| b.is_ascii_alphabetic()),
        "FORMULA_X" => is_formula(s, 'x'),
        "FORMULA_Y" => is_formula(s, 'y'),
        "HTTP_URL" => is_http_url(s),
        "URL_SCHEME" => has_url_scheme(s),
        other => panic!("opRegistry: no matcher for regex \"{other}\""),
    }
}
