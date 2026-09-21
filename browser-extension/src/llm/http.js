// Extension copy of browser/js/llm/http.js: the typed LlmError, the provider-prose
// sanitizer and the one JSON POST every provider goes through. Byte-pinned (portParity).

// Typed error the chat UI renders instead of parsing a plan. Kinds: 'truncated', 'refusal',
// 'disabled' (503 llmDisabled), 'badReply' (2xx off-shape), 'network' (fetch itself) and 'http'.
export class LlmError extends Error {
  constructor(message, kind) { super(message); this.name = 'LlmError'; this.kind = kind; }
  static config(message) { return new LlmError(message, 'config'); }
  static network(message) { return new LlmError(message, 'network'); }
  static http(message) { return new LlmError(message, 'http'); }
  static badReply(message) { return new LlmError(message, 'badReply'); }
  static truncated(message) { return new LlmError(message, 'truncated'); }
  static refusal(message) { return new LlmError(message, 'refusal'); }
  static disabled(message) { return new LlmError(message, 'disabled'); }
}

// How much of a provider's own prose an error may quote (server upstream.go parity).
const MAX_PROVIDER_DETAIL = 200;

// A provider's error prose is untrusted: control characters out, URLs and token-shaped runs
// redacted (an endpoint may echo the key back), whitespace collapsed, hard-truncated.
export const sanitizeProviderText = (text) => {
  let t = String(text ?? '').slice(0, 4 * MAX_PROVIDER_DETAIL);
  t = t.replace(/[\p{Cc}\p{Cf}]/gu, ' ');
  t = t.replace(/[a-z][a-z0-9+.-]*:\/\/\S+/gi, '[redacted]');
  t = t.replace(/(?:bearer|basic) +[A-Za-z0-9._~+/=-]{8,}|\b(?:sk|pk|api[-_]?key|key|token|secret)[-_=:][A-Za-z0-9._-]{6,}|[A-Za-z0-9_-]{24,}/gi, '[redacted]');
  t = t.split(/\s+/).filter(Boolean).join(' ');
  return t.length > MAX_PROVIDER_DETAIL ? `${t.slice(0, MAX_PROVIDER_DETAIL - 1).trim()}…` : t;
};

export const postJson = async (fetchImpl, url, body, headers = {}, signal = undefined) => {
  if (!fetchImpl) throw LlmError.http('no fetch implementation available');
  let resp;
  try {
    resp = await fetchImpl(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', ...headers },
      body: JSON.stringify(body),
      ...(signal ? { signal } : {}),
    });
  } catch (err) {
    if (err?.name === 'AbortError') throw err;   // Stop button — not a failure
    // fetch's own failures (DNS, refused, CORS) are TypeErrors — tagged HERE so a TypeError thrown
    // later in plan execution never reads as "unreachable".
    throw LlmError.network(err?.message || String(err));
  }
  if (!resp.ok) {
    let msg = `HTTP ${resp.status}`;
    let code = '';
    try {
      // Every provider's error shape (desktop llmClient parity): server {message, code}, ollama
      // {"error":"…"}, openai-compat {"error":{"message":"…"}} — a missing model reads as itself.
      const e = await resp.json();
      if (e && e.message) { msg = e.message; code = e.code || ''; }
      else if (typeof e?.error === 'string' && e.error) msg = e.error;
      else if (e?.error?.message) msg = e.error.message;
      // Only the provider's own words survive, bounded — never the raw body.
      msg = sanitizeProviderText(msg) || `HTTP ${resp.status}`;
    } catch { /* non-JSON error body */ }
    const err = code === 'llmDisabled' ? LlmError.disabled(msg) : LlmError.http(msg);
    err.answered = true;   // the endpoint responded — this is NOT "unreachable"
    err.status = resp.status;   // 401/403 = the SESSION is over, not the provider
    throw err;
  }
  return resp.json();
};
