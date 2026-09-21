// ── LLM chat client (llm-contract.md §6 wire mappings) ─────────────────
// Extension copy of browser/js/llm/client.js (no cross-subproject imports). One
// chat({ system, messages }) over the three providers; messages use the stencil-server DTO shape
// as canonical. The two copies must stay identical BELOW this header (portParity.test.js pins
// them); anything per-surface lives in surface.js. fetch is injected for `node --test`.
import PROVIDERS_ASSET from '../config/providers.json' with { type: 'json' };
import { ASSISTANT_OFF_TEXT, defaultGetToken } from './surface.js';
import { LlmError, postJson } from './http.js';
export { LlmError, sanitizeProviderText } from './http.js';

// Endpoint paths, display names and the probe timeout come from config providers.json (the
// extension ships a checked-in copy, pinned by its dataParity.test.js).
const PROVIDER_INFO = PROVIDERS_ASSET.providers;
const PROBE_TIMEOUT_MS = PROVIDERS_ASSET.timeouts.probeMs;

// Human names for status lines ("Ollama @ localhost:11434 — connected").
// 'none' is the local-only off state — not a provider, so not in providers.json.
export const PROVIDER_LABELS = Object.freeze({
  none: 'None (turned off)',
  ...Object.fromEntries(Object.entries(PROVIDER_INFO).map(([id, p]) => [id, p.displayName])),
});

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

// `getToken(serverUrl)` resolves the existing Stencil bearer token for stencil-server (sync or
// async); without one the surface's defaultGetToken applies (surface.js).
export const createLlmClient = ({ settings, fetchImpl = globalThis.fetch?.bind(globalThis), getToken } = {}) => {
  const s = settings || {};
  const resolveToken = getToken || defaultGetToken(s);

  // `signal` (optional AbortSignal) cancels the in-flight request — the panel's
  // Stop button rides on it; aborts surface as the runtime's AbortError.
  const chat = async ({ system, messages, signal }) => {
    const msgs = messages || [];

    if (s.provider === 'none') throw LlmError.config(ASSISTANT_OFF_TEXT);

    if (s.provider === 'ollama') {
      if (!s.baseUrl) throw LlmError.http('No Ollama base URL configured');
      const r = await postJson(fetchImpl, `${s.baseUrl}${PROVIDER_INFO.ollama.chatPath}`, {
        model: s.model || '',
        stream: false,
        messages: [{ role: 'system', content: system }, ...msgs.map(ollamaMessage)],
      }, {}, signal);
      // A 2xx body without a reply string (e.g. an error-shaped {"error":…}) is a
      // typed badReply, never a silent "" (parity with the other surfaces).
      const content = r.message?.content;
      if (typeof content !== 'string') throw LlmError.badReply('malformed ollama response (no message.content)');
      return content;
    }

    if (s.provider === 'openai-compat') {
      if (!s.baseUrl) throw LlmError.http('No base URL configured');
      const headers = s.apiKey ? { Authorization: 'Bearer ' + s.apiKey } : {};
      const r = await postJson(fetchImpl, `${s.baseUrl}${PROVIDER_INFO['openai-compat'].chatPath}`, {
        model: s.model || '',
        stream: false,
        messages: [{ role: 'system', content: system }, ...msgs.map(openaiMessage)],
      }, headers, signal);
      const content = r.choices?.[0]?.message?.content;
      if (typeof content !== 'string') throw LlmError.badReply('malformed response (no choices[0].message.content)');
      return content;
    }

    if (s.provider === 'stencil-server') {
      if (!s.serverUrl) throw LlmError.http('No Stencil server configured for the assistant');
      const token = await resolveToken(s.serverUrl);
      const body = { system, messages: msgs.map(serverMessage) };
      if (s.model) body.model = s.model;
      const r = await postJson(fetchImpl, `${s.serverUrl}${PROVIDER_INFO['stencil-server'].chatPath}`, body, { Authorization: 'Bearer ' + token }, signal);
      // stopReason handling per contract §6.3: truncated/refused replies are typed
      // errors for the chat UI to render — NEVER parsed as an op-plan.
      if (r.stopReason === 'max_tokens') throw LlmError.truncated('Response truncated — the model hit its output limit; try a shorter request');
      if (r.stopReason === 'refusal') throw LlmError.refusal(r.text || 'The model refused this request');
      if (typeof r.text !== 'string') throw LlmError.badReply('malformed server response (no text)');
      return r.text;
    }

    throw LlmError.http(`Unknown LLM provider "${s.provider}"`);
  };

  return { chat };
};

// GET {serverUrl}/llm/info → { enabled, model } so the settings UI can render
// "via server X (model)". Errors propagate (the caller shows "unreachable").
export const fetchLlmInfo = async (serverUrl, { token = '', fetchImpl = globalThis.fetch?.bind(globalThis) } = {}) => {
  if (!fetchImpl) throw LlmError.http('no fetch implementation available');
  const resp = await fetchImpl(`${serverUrl}${PROVIDER_INFO['stencil-server'].infoPath}`, { headers: { Authorization: 'Bearer ' + token } });
  if (!resp.ok) throw LlmError.http(`HTTP ${resp.status}`);
  return resp.json();
};

// Model suggestions for the settings datalist. Best-effort: NEVER throws, failures resolve [] —
// the Model field always stays free-form typing.
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

// Cheap reachability probe, no chat tokens spent. NEVER throws: failures resolve
// { ok: false, detail }. Short timeout so a status refresh cannot hang.
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
