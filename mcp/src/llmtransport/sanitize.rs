//! What a non-2xx body is allowed to say (contract §6.3): the provider's own message and
//! error code, with anything credential-shaped struck out, and the shared clipper.
//!
//! This is a cross-surface contract — the sanitizer fixture corpus pins the same behaviour
//! on every client, so a change here is a change to all of them.

/// How much of a quoted body is included in an error message — a bad LLM reply in `llm`,
/// an invalid op-plan in `server`.
pub(crate) const SNIPPET_LEN: usize = 400;

/// How much of a provider's own prose an error may quote (contract §6.3).
const DETAIL_LEN: usize = 200;

/// The provider's own message out of a non-2xx body — the §6 shapes every client parses —
/// sanitized by [`sanitize_detail`]. The raw body is NEVER shown to the user.
pub fn error_reason(body: &str) -> String {
    let Ok(value) = serde_json::from_str::<serde_json::Value>(body) else {
        return String::new();
    };
    let raw = value
        .get("message")
        .and_then(|v| v.as_str())
        .or_else(|| match value.get("error") {
            Some(serde_json::Value::String(s)) => Some(s.as_str()),
            Some(e) => e.get("message").and_then(|v| v.as_str()),
            None => None,
        });
    raw.map(sanitize_detail).unwrap_or_default()
}

/// The machine `code` out of a non-2xx body (the stencil-server error shape, e.g.
/// "llmDisabled") — "" when the body is not JSON or carries none.
pub fn error_code(body: &str) -> String {
    serde_json::from_str::<serde_json::Value>(body)
        .ok()
        .and_then(|v| v.get("code").and_then(|c| c.as_str()).map(str::to_string))
        .unwrap_or_default()
}

/// Untrusted provider prose made safe to quote: control characters out, URL- and
/// token-shaped words redacted, whitespace collapsed, cut at `DETAIL_LEN` on a word.
pub fn sanitize_detail(text: &str) -> String {
    let head: String = text.chars().take(4 * DETAIL_LEN).collect();
    let words: Vec<&str> = head
        .split(|c: char| c.is_whitespace() || c.is_control())
        .filter(|w| !w.is_empty())
        .collect();
    let mut out = String::new();
    let mut len = 0; // characters written
    let mut i = 0;
    while i < words.len() {
        let raw = words[i];
        // `Bearer <token>` / `Basic <token>` collapse to ONE [redacted] (browser parity).
        let (word, used) = if is_auth_scheme(raw)
            && words.get(i + 1).is_some_and(|w| is_auth_param(w))
        {
            ("[redacted]", 2)
        } else if secretish(raw) {
            ("[redacted]", 1)
        } else {
            (raw, 1)
        };
        let sep = usize::from(!out.is_empty());
        let n = word.chars().count();
        if len + sep + n > DETAIL_LEN {
            out.push('…');
            return out;
        }
        if sep == 1 {
            out.push(' ');
        }
        out.push_str(word);
        len += sep + n;
        i += used;
    }
    out
}

/// An HTTP auth scheme whose following credential must never be echoed.
fn is_auth_scheme(word: &str) -> bool {
    word.eq_ignore_ascii_case("bearer") || word.eq_ignore_ascii_case("basic")
}

/// The credential after `Bearer`/`Basic`: 8+ chars of the token68-ish alphabet the
/// browser's `(?:bearer|basic) +[A-Za-z0-9._~+/=-]{8,}` rule accepts.
fn is_auth_param(word: &str) -> bool {
    word.chars().count() >= 8
        && word
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '~' | '+' | '/' | '=' | '-'))
}

/// True for a word that must not be echoed back: an absolute URL (an internal endpoint is
/// not the caller's business), a long opaque run, or a `key=`/`sk-`-style credential.
fn secretish(word: &str) -> bool {
    fn token_chars(s: &str) -> bool {
        s.chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-'))
    }
    if word.contains("://") || (word.len() >= 24 && token_chars(word)) {
        return true;
    }
    // Known credential heads (compound ones like api_key first, so `api_key=…` is not
    // mis-split at the `_` after `api`), then a separator and 6+ token chars.
    const HEADS: [&str; 8] =
        ["api_key", "api-key", "apikey", "sk", "pk", "key", "token", "secret"];
    let lower = word.to_ascii_lowercase();
    HEADS.iter().any(|head| {
        lower.strip_prefix(head).is_some_and(|rest| {
            rest.starts_with(['-', '_', '=', ':']) && rest.len() - 1 >= 6 && token_chars(&rest[1..])
        })
    })
}

/// Bound `text` to at most `max` bytes (cut on a char boundary, `…` appended) so it can be
/// quoted in an error message. Shared with `llm`'s bad-reply errors.
pub(crate) fn clip(text: &str, max: usize) -> String {
    let trimmed = text.trim();
    if trimmed.len() <= max {
        return trimmed.to_string();
    }
    let mut end = max;
    while !trimmed.is_char_boundary(end) {
        end -= 1;
    }
    format!("{}…", &trimmed[..end])
}
