// ── LLM chat client (llm-contract.md §6 wire mappings) ─────────────────
// One chat({ system, messages }) method over the three providers. Messages use the
// stencil-server DTO shape as the canonical form ({ role, text, images: [{ mediaType,
// data }] }); the ollama / openai-compat bodies are derived from it. fetch is injected
// for `node --test`, mirroring connectionManager.js.
//
// SHARED with extension/src/llm/llmClient.js: the two copies must stay identical
// below this header (extension/tests/portParity.test.js pins them); anything
// per-surface lives in llmSurface.js.
import PROVIDERS_ASSET from '../config/llm/providers.json' with { type: 'json' };
import { ASSISTANT_OFF_TEXT, defaultGetToken } from './llmSurface.js';

// Typed error the chat UI renders instead of parsing a plan:
//   kind 'truncated' — stencil-server stopReason max_tokens (never parsed as a plan)
//   kind 'refusal'   — stencil-server stopReason refusal (shown as a chat error)
//   kind 'disabled'  — the server has no LLM key configured (503 llmDisabled)
//   kind 'badReply'  — a 2xx body outside the documented shape (never an empty reply)
//   kind 'network'   — fetch itself failed (DNS, refused, CORS), tagged at the call
//   kind 'http'      — any other transport/HTTP failure
export class LlmError extends Error {
  constructor(message, kind) {
    super(message);
    this.name = 'LlmError';
    this.kind = kind;
  }
}

// Endpoint paths, display names and the probe timeout come from the shared
// constants file config providers.json (the extension ships a checked-in copy,
// pinned by its dataParity.test.js).
const PROVIDER_INFO = PROVIDERS_ASSET.providers;
const PROBE_TIMEOUT_MS = PROVIDERS_ASSET.timeouts.probeMs;

// Human names for status lines ("Ollama @ localhost:11434 — connected").
// 'none' is the local-only off state — not a provider, so not in providers.json.
export const PROVIDER_LABELS = Object.freeze({
  none: 'None (turned off)',
  ...Object.fromEntries(Object.entries(PROVIDER_INFO).map(([id, p]) => [id, p.displayName])),
});

// How much of a provider's own prose an error may quote (server upstream.go parity).
const MAX_PROVIDER_DETAIL = 200;

// A provider's error prose is untrusted text: control characters out, URLs and
// token-shaped runs redacted (an endpoint may echo the key back), whitespace
// collapsed, hard-truncated. Port of the server's sanitizeUpstreamText.
export const sanitizeProviderText = (text) => {
  let t = String(text ?? '').slice(0, 4 * MAX_PROVIDER_DETAIL);
  t = t.replace(/[\p{Cc}\p{Cf}]/gu, ' ');
  t = t.replace(/[a-z][a-z0-9+.-]*:\/\/\S+/gi, '[redacted]');
  t = t.replace(/(?:bearer|basic) +[A-Za-z0-9._~+/=-]{8,}|\b(?:sk|pk|api[-_]?key|key|token|secret)[-_=:][A-Za-z0-9._-]{6,}|[A-Za-z0-9_-]{24,}/gi, '[redacted]');
  t = t.split(/\s+/).filter(Boolean).join(' ');
  return t.length > MAX_PROVIDER_DETAIL ? `${t.slice(0, MAX_PROVIDER_DETAIL - 1).trim()}…` : t;
};

const postJson = async (fetchImpl, url, body, headers = {}, signal = undefined) => {
  if (!fetchImpl) throw new LlmError('no fetch implementation available', 'http');
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
    // fetch's own failures (DNS, refused, CORS) are TypeErrors — tag them HERE
    // so the error mapping never has to guess from the exception type (a
    // TypeError thrown later in plan execution must not read as "unreachable").
    throw new LlmError(err?.message || String(err), 'network');
  }
  if (!resp.ok) {
    let msg = `HTTP ${resp.status}`;
    let code = '';
    try {
      // Every provider's error shape (desktop llmClient parity): server {message, code},
      // ollama {"error":"…"}, openai-compat {"error":{"message":"…"}} — a missing
      // model reads as itself, not as a bare status code.
      const e = await resp.json();
      if (e && e.message) { msg = e.message; code = e.code || ''; }
      else if (typeof e?.error === 'string' && e.error) msg = e.error;
      else if (e?.error?.message) msg = e.error.message;
      // Only the provider's own words survive, bounded — never the raw body.
      msg = sanitizeProviderText(msg) || `HTTP ${resp.status}`;
    } catch { /* non-JSON error body */ }
    const err = new LlmError(msg, code === 'llmDisabled' ? 'disabled' : 'http');
    err.answered = true;   // the endpoint responded — this is NOT "unreachable"
    err.status = resp.status;   // 401/403 = the SESSION is over, not the provider
    throw err;
  }
  return resp.json();
};

// Canonical message → ollama native chat message (images as bare base64 strings).
const ollamaMessage = (m) => (m.images && m.images.length
  ? { role: m.role, content: m.text, images: m.images.map((i) => i.data) }
  : { role: m.role, content: m.text });

// Canonical message → openai-compat message (images as image_url data: URLs).
const openaiMessage = (m) => (m.images && m.images.length
  ? {
    role: m.role,
    content: [
      { type: 'text', text: m.text },
      ...m.images.map((i) => ({ type: 'image_url', image_url: { url: `data:${i.mediaType};base64,${i.data}` } })),
    ],
  }
  : { role: m.role, content: m.text });

// Canonical message → stencil-server protocol.LlmMessage (images omitted when empty).
const serverMessage = (m) => (m.images && m.images.length
  ? { role: m.role, text: m.text, images: m.images }
  : { role: m.role, text: m.text });

// Build a client for the given settings. `getToken(serverUrl)` resolves the existing
// Stencil bearer token for the stencil-server provider (sync or async); without one
// the surface's defaultGetToken applies (llmSurface.js). Injectable for `node --test`.
export const createLlmClient = ({ settings, fetchImpl = globalThis.fetch?.bind(globalThis), getToken } = {}) => {
  const s = settings || {};
  const resolveToken = getToken || defaultGetToken(s);

  // `signal` (optional AbortSignal) cancels the in-flight request — the panel's
  // Stop button rides on it; aborts surface as the runtime's AbortError.
  const chat = async ({ system, messages, signal }) => {
    const msgs = messages || [];

    if (s.provider === 'none') {
      throw new LlmError(ASSISTANT_OFF_TEXT, 'config');
    }

    if (s.provider === 'ollama') {
      if (!s.baseUrl) throw new LlmError('No Ollama base URL configured', 'http');
      const r = await postJson(fetchImpl, `${s.baseUrl}${PROVIDER_INFO.ollama.chatPath}`, {
        model: s.model || '',
        stream: false,
        messages: [{ role: 'system', content: system }, ...msgs.map(ollamaMessage)],
      }, {}, signal);
      // A 2xx body without a reply string (e.g. an error-shaped {"error":…}) is a
      // typed badReply, never a silent "" (parity with the other surfaces).
      const content = r.message?.content;
      if (typeof content !== 'string') throw new LlmError('malformed ollama response (no message.content)', 'badReply');
      return content;
    }

    if (s.provider === 'openai-compat') {
      if (!s.baseUrl) throw new LlmError('No base URL configured', 'http');
      const headers = s.apiKey ? { Authorization: 'Bearer ' + s.apiKey } : {};
      const r = await postJson(fetchImpl, `${s.baseUrl}${PROVIDER_INFO['openai-compat'].chatPath}`, {
        model: s.model || '',
        stream: false,
        messages: [{ role: 'system', content: system }, ...msgs.map(openaiMessage)],
      }, headers, signal);
      const content = r.choices?.[0]?.message?.content;
      if (typeof content !== 'string') throw new LlmError('malformed response (no choices[0].message.content)', 'badReply');
      return content;
    }

    if (s.provider === 'stencil-server') {
      if (!s.serverUrl) throw new LlmError('No Stencil server configured for the assistant', 'http');
      const token = await resolveToken(s.serverUrl);
      const body = { system, messages: msgs.map(serverMessage) };
      if (s.model) body.model = s.model;
      const r = await postJson(fetchImpl, `${s.serverUrl}${PROVIDER_INFO['stencil-server'].chatPath}`, body, { Authorization: 'Bearer ' + token }, signal);
      // stopReason handling per contract §6.3: truncated/refused replies are typed
      // errors for the chat UI to render — NEVER parsed as an op-plan.
      if (r.stopReason === 'max_tokens') throw new LlmError('Response truncated — the model hit its output limit; try a shorter request', 'truncated');
      if (r.stopReason === 'refusal') throw new LlmError(r.text || 'The model refused this request', 'refusal');
      if (typeof r.text !== 'string') throw new LlmError('malformed server response (no text)', 'badReply');
      return r.text;
    }

    throw new LlmError(`Unknown LLM provider "${s.provider}"`, 'http');
  };

  return { chat };
};

// GET {serverUrl}/llm/info → { enabled, model } so the settings UI can render
// "via server X (model)". Errors propagate (the caller shows "unreachable").
export const fetchLlmInfo = async (serverUrl, { token = '', fetchImpl = globalThis.fetch?.bind(globalThis) } = {}) => {
  if (!fetchImpl) throw new LlmError('no fetch implementation available', 'http');
  const resp = await fetchImpl(`${serverUrl}${PROVIDER_INFO['stencil-server'].infoPath}`, {
    headers: { Authorization: 'Bearer ' + token },
  });
  if (!resp.ok) throw new LlmError(`HTTP ${resp.status}`, 'http');
  return resp.json();
};

// Model suggestions for the settings UI's datalist (ollama GET /api/tags, openai-compat
// GET /models, stencil-server /llm/info's default). Best-effort: NEVER throws, failures
// resolve [] — the Model field always stays free-form typing.
export const listModels = async (settings, { fetchImpl = globalThis.fetch?.bind(globalThis), getToken, timeoutMs = PROBE_TIMEOUT_MS } = {}) => {
  const s = settings || {};
  if (!fetchImpl) return [];
  const aborter = typeof AbortController !== 'undefined' ? new AbortController() : null;
  const timer = aborter ? setTimeout(() => aborter.abort(), timeoutMs) : null;
  const signal = aborter ? { signal: aborter.signal } : {};
  try {
    if (s.provider === 'ollama' && s.baseUrl) {
      const resp = await fetchImpl(`${s.baseUrl}/api/tags`, { ...signal });
      if (!resp.ok) return [];
      const v = await resp.json().catch(() => ({}));
      return (Array.isArray(v.models) ? v.models : []).map((m) => m?.name).filter(Boolean);
    }
    if (s.provider === 'openai-compat' && s.baseUrl) {
      const headers = s.apiKey ? { Authorization: 'Bearer ' + s.apiKey } : {};
      const resp = await fetchImpl(`${s.baseUrl}/models`, { ...signal, headers });
      if (!resp.ok) return [];
      const v = await resp.json().catch(() => ({}));
      return (Array.isArray(v.data) ? v.data : []).map((m) => m?.id).filter(Boolean);
    }
    if (s.provider === 'stencil-server' && s.serverUrl) {
      const token = getToken ? await getToken(s.serverUrl) : '';
      const info = await fetchLlmInfo(s.serverUrl, {
        token,
        fetchImpl: (u, init) => fetchImpl(u, { ...signal, ...init }),
      }).catch(() => null);
      return info?.model ? [info.model] : [];
    }
    return [];
  } catch {
    return [];
  } finally {
    if (timer) clearTimeout(timer);
  }
};

// Cheap reachability probe for the configured provider — no chat tokens spent:
// ollama GET /api/version · openai-compat GET /models · stencil-server GET /llm/info.
// NEVER throws: failures resolve { ok: false, detail }. Short timeout so a status
// refresh can't hang; fetch is injected for `node --test`.
export const probeProvider = async (settings, { fetchImpl = globalThis.fetch?.bind(globalThis), getToken, timeoutMs = PROBE_TIMEOUT_MS } = {}) => {
  const s = settings || {};
  const url = s.provider === 'stencil-server' ? (s.serverUrl || '') : (s.baseUrl || '');
  const base = { provider: s.provider, url, model: s.model || '' };
  const fail = (detail) => ({ ...base, ok: false, detail });
  if (s.provider === 'none') return fail('assistant turned off');
  if (!url) return fail('not configured');
  if (!fetchImpl) return fail('no fetch implementation available');
  const aborter = typeof AbortController !== 'undefined' ? new AbortController() : null;
  const timer = aborter ? setTimeout(() => aborter.abort(), timeoutMs) : null;
  const signal = aborter ? { signal: aborter.signal } : {};
  try {
    if (s.provider === 'ollama') {
      const resp = await fetchImpl(`${s.baseUrl}/api/version`, { ...signal });
      if (!resp.ok) return fail(`HTTP ${resp.status}`);
      const v = await resp.json().catch(() => ({}));
      return { ...base, ok: true, detail: v.version ? `v${v.version}` : '' };
    }
    if (s.provider === 'openai-compat') {
      const headers = s.apiKey ? { Authorization: 'Bearer ' + s.apiKey } : {};
      const resp = await fetchImpl(`${s.baseUrl}/models`, { ...signal, headers });
      if (!resp.ok) return fail(`HTTP ${resp.status}`);
      const v = await resp.json().catch(() => ({}));
      const first = Array.isArray(v.data) && v.data[0]?.id ? v.data[0].id : '';
      return { ...base, ok: true, detail: first };
    }
    if (s.provider === 'stencil-server') {
      const token = getToken ? await getToken(s.serverUrl) : '';
      const info = await fetchLlmInfo(s.serverUrl, {
        token,
        fetchImpl: (u, init) => fetchImpl(u, { ...signal, ...init }),
      });
      if (!info.enabled) return fail('LLM disabled on this server (no API key configured)');
      return { ...base, ok: true, detail: info.model || '' };
    }
    return fail(`unknown provider "${s.provider}"`);
  } catch (err) {
    return fail(err?.message || 'not reachable');
  } finally {
    if (timer) clearTimeout(timer);
  }
};
