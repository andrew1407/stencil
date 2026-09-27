// Extension copy of browser/js/llm/http.js: the typed LlmError, the provider-prose
// sanitizer, the server's upstream classifier, the keyed wire's transport rule and the one
// JSON POST and info GET every provider goes through. Byte-pinned (portParity).
import { MAX_ERROR_BYTES, readJsonCapped } from '../lib/connection/cappedBody.js';

// Typed error the chat UI renders instead of parsing a plan. Kinds: 'truncated', 'refusal', 'disabled'
// (503 llmDisabled, or no session key), 'badReply' (2xx off-shape), 'network' (fetch itself) and 'http'.
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

// 8 MiB, the most a provider's reply buffers: the cap cli/src/llm/check.zig and mcp's transport read to.
export const MAX_REPLY_BYTES = 8 * 1024 * 1024;
export const readReply = (resp) => readJsonCapped(resp, MAX_REPLY_BYTES);

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

// server/internal/llm/upstream.go's reasons, each the whole message; a direct wire's rate limit is the user's key.
const UPSTREAM_REASONS = Object.freeze({
  credits: 'the LLM provider is out of credits or has no active billing',
  auth: 'the LLM provider rejected the API key',
  model: 'the LLM provider does not have the requested model',
  rateLimit: 'the LLM provider is rate-limiting this key',
  timeout: 'the LLM provider did not respond in time',
  overloaded: 'the LLM provider is temporarily unavailable',
});

const hasAny = (s, subs) => subs.some((sub) => s.includes(sub));

// upstream.go classifyUpstream, in its order: billing first, since a spent balance arrives as a 400
// or a 429 depending on vendor. '' = unrecognised.
export const classifyUpstream = (status, errType = '', message = '') => {
  const t = String(errType).toLowerCase();
  const m = String(message).toLowerCase();
  if (hasAny(t, ['insufficient_quota', 'billing', 'credit']) || status === 402
    || hasAny(m, ['credit balance', 'insufficient_quota', 'insufficient quota', 'purchase credits', 'billing', 'out of credits'])) return 'credits';
  if (hasAny(t, ['authentication', 'invalid_api_key', 'permission', 'unauthorized', 'forbidden']) || status === 401 || status === 403) return 'auth';
  if (hasAny(t, ['model_not_found', 'not_found']) || status === 404
    || hasAny(m, ['model not found', 'unknown model', 'does not exist', 'try pulling', 'no such model'])) return 'model';
  if (t.includes('rate_limit') || status === 429) return 'rateLimit';
  if (status === 408 || status === 504) return 'timeout';
  if (hasAny(t, ['overloaded', 'api_error']) || status >= 500) return 'overloaded';
  return '';
};

// sanitize.go containsSecretFragment: any 8-character run of the key is still a leak.
export const containsSecretFragment = (text, secret) => {
  const key = String(secret ?? '');
  for (let i = 0; i + 8 <= key.length; i++) if (String(text).includes(key.slice(i, i + 8))) return true;
  return false;
};

// A recognised condition is said once; anything else carries its status and the upstream's own
// sanitized words, dropped whole when they echo the key (upstream.go ClientMessage).
export const upstreamErrorText = (status, errType, message, secret) => {
  const kind = classifyUpstream(status, errType, message);
  if (kind) return UPSTREAM_REASONS[kind];
  const head = `the LLM provider returned an error${status > 0 ? ` (HTTP ${status})` : ''}`;
  const detail = sanitizeProviderText(message);
  return detail && !containsSecretFragment(detail, secret) ? `${head}: ${detail}` : head;
};

// The Anthropic envelope {type:"error", error:{type, message}}; without a message neither field counts.
export const upstreamFailure = (secret) => (status, e) => {
  const message = typeof e?.error?.message === 'string' ? e.error.message : '';
  return { message: upstreamErrorText(status, message ? String(e.error.type ?? '') : '', message, secret) };
};

// Every other provider's error shape (desktop llmClient parity): server {message, code}, ollama
// {"error":"…"}, openai-compat {"error":{"message":"…"}} — a missing model reads as itself.
const describeError = (status, e) => {
  let msg = '';
  let code = '';
  if (e?.message) { msg = e.message; code = e.code || ''; }
  else if (typeof e?.error === 'string' && e.error) msg = e.error;
  else if (e?.error?.message) msg = e.error.message;
  // Only the provider's own words survive, bounded — never the raw body.
  return { message: sanitizeProviderText(msg) || `HTTP ${status}`, disabled: code === 'llmDisabled' };
};

// §6.5: a key rides plain http only to a loopback host (a local mock or proxy the user named).
// A relative URL is judged where fetch would send it.
export const keyedInit = (url, isLoopbackHost) => {
  let u = null;
  try { u = new URL(url, globalThis.location?.href); } catch { /* fetch refuses it too */ }
  if (u?.protocol === 'http:' && !isLoopbackHost(u.hostname)) {
    const host = u.hostname.replace(/^\[|\]$/g, '');
    throw LlmError.disabled(`refusing to send the API key to '${host}' over plain http — use https`);
  }
  return {};
};

// No provider's 30x is followed: a key or bearer token must not ride it to a second host (§6.5).
export const NO_REDIRECT = 'error';

export const postJson = async (fetchImpl, url, body, headers = {}, signal = undefined, describe = describeError, init = {}) => {
  if (!fetchImpl) throw LlmError.http('no fetch implementation available');
  let resp;
  try {
    resp = await fetchImpl(url, {
      ...init,
      method: 'POST',
      headers: { 'Content-Type': 'application/json', ...headers },
      body: JSON.stringify(body),
      redirect: NO_REDIRECT,
      ...(signal ? { signal } : {}),
    });
  } catch (err) {
    if (err?.name === 'AbortError') throw err;   // Stop button — not a failure
    // fetch's own failures (DNS, refused, CORS) are TypeErrors — tagged HERE so a TypeError thrown
    // later in plan execution never reads as "unreachable".
    throw LlmError.network(err?.message || String(err));
  }
  if (!resp.ok) {
    let e = null;
    try { e = await readJsonCapped(resp, MAX_ERROR_BYTES); } catch { /* non-JSON or oversize error body */ }
    const { message, disabled } = describe(resp.status, e);
    const err = disabled ? LlmError.disabled(message) : LlmError.http(message);
    err.answered = true;   // the endpoint responded — this is NOT "unreachable"
    err.status = resp.status;   // 401/403 = the SESSION is over, not the provider
    throw err;
  }
  return readReply(resp);
};

// GET {base}{path} under the probe timeout → { ok, status, body }; a non-JSON body reads as {}.
export const getInfo = async (fetchImpl, url, headers, timeoutMs, init = {}) => {
  const aborter = typeof AbortController !== 'undefined' ? new AbortController() : null;
  const timer = aborter ? setTimeout(() => aborter.abort(), timeoutMs) : null;
  try {
    const resp = await fetchImpl(url, { ...init, redirect: NO_REDIRECT, ...(aborter ? { signal: aborter.signal } : {}), headers });
    return { ok: resp.ok, status: resp.status, body: resp.ok ? await readReply(resp).catch(() => ({})) : null };
  } finally {
    if (timer) clearTimeout(timer);
  }
};
