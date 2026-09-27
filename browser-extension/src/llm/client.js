// ── LLM chat client (llm-contract.md §6 wire mappings) ─────────────────
// Extension copy of browser/js/llm/client.js (no cross-subproject imports). One
// chat({ system, messages }) over the four wires; messages use the stencil-server DTO shape
// as canonical. The two copies must stay identical BELOW this header (portParity.test.js pins
// them); anything per-surface lives in surface.js. fetch is injected for `node --test`.
import PROVIDERS_ASSET from '../config/providers.json' with { type: 'json' };
import { ASSISTANT_OFF_TEXT, defaultGetToken, isLoopbackHost } from './surface.js';
import { LlmError, NO_REDIRECT, getInfo, keyedInit, postJson, readReply, upstreamFailure } from './http.js';
export { LlmError, sanitizeProviderText } from './http.js';

// Endpoint paths, display names and the probe timeout come from config providers.json (the
// extension ships a checked-in copy, pinned by its dataParity.test.js).
const PROVIDER_INFO = PROVIDERS_ASSET.providers;
const PROBE_TIMEOUT_MS = PROVIDERS_ASSET.timeouts.probeMs;
const { anthropicUpstream: ANTHROPIC, serverDefaults: ANTHROPIC_DEFAULTS } = PROVIDERS_ASSET;
export const NO_KEY_TEXT = 'no API key for this session';
const TRUNCATED_TEXT = 'Response truncated — the model hit its output limit; try a shorter request';

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

// Canonical message → Anthropic content blocks: the text (none when empty), then each image.
const anthropicImage = (i) => ({ type: 'image', source: { type: 'base64', media_type: i.mediaType, data: i.data } });
const anthropicMessage = (m) => ({
  role: m.role, content: [...(m.text ? [{ type: 'text', text: m.text }] : []), ...(m.images || []).map(anthropicImage)],
});

const bearer = (secret) => (secret ? { Authorization: 'Bearer ' + secret } : {});
const listOf = (v) => (Array.isArray(v) ? v : []);

// One strategy per providers.json `wire`: the configured base and its unset message, the auth
// headers (`session`: from the Stencil session token; `keyed`: nothing is sent without apiKey, `init`
// its fetch options or refusal), the chat body, reply and failure text, and what models and probe answer.
const WIRES = Object.freeze({
  ollama: {
    base: (s) => s.baseUrl,
    unset: 'No Ollama base URL configured',
    auth: () => ({}),
    body: (s, system, msgs) => ({
      model: s.model || '',
      stream: false,
      messages: [{ role: 'system', content: system }, ...msgs.map(ollamaMessage)],
    }),
    // A 2xx body without a reply string (e.g. an error-shaped {"error":…}) is a
    // typed badReply, never a silent "" (parity with the other surfaces).
    reply: (r) => {
      const content = r.message?.content;
      if (typeof content !== 'string') throw LlmError.badReply('malformed ollama response (no message.content)');
      return content;
    },
    models: (v) => listOf(v.models).map((m) => m?.name),
    probe: (v) => ({ ok: true, detail: v.version ? `v${v.version}` : '' }),
  },
  openai: {
    base: (s) => s.baseUrl,
    unset: 'No base URL configured',
    auth: (s) => bearer(s.apiKey),
    body: (s, system, msgs) => ({
      model: s.model || '',
      stream: false,
      messages: [{ role: 'system', content: system }, ...msgs.map(openaiMessage)],
    }),
    reply: (r) => {
      const content = r.choices?.[0]?.message?.content;
      if (typeof content !== 'string') throw LlmError.badReply('malformed response (no choices[0].message.content)');
      return content;
    },
    models: (v) => listOf(v.data).map((m) => m?.id),
    probe: (v) => ({ ok: true, detail: listOf(v.data)[0]?.id || '' }),
  },
  server: {
    base: (s) => s.serverUrl,
    unset: 'No Stencil server configured for the assistant',
    session: true,
    auth: (s, token) => ({ Authorization: 'Bearer ' + token }),
    body: (s, system, msgs) => ({ system, messages: msgs.map(serverMessage), ...(s.model ? { model: s.model } : {}) }),
    // stopReason handling per contract §6.3: truncated/refused replies are typed
    // errors for the chat UI to render — NEVER parsed as an op-plan.
    reply: (r) => {
      if (r.stopReason === 'max_tokens') throw LlmError.truncated(TRUNCATED_TEXT);
      if (r.stopReason === 'refusal') throw LlmError.refusal(r.text || 'The model refused this request');
      if (typeof r.text !== 'string') throw LlmError.badReply('malformed server response (no text)');
      return r.text;
    },
    models: (v) => (v?.model ? [v.model] : []),
    probe: (v) => (v.enabled
      ? { ok: true, detail: v.model || '' }
      : { ok: false, detail: 'LLM disabled on this server (no API key configured)' }),
  },
  // §6.5: straight to Anthropic with the session key; this client always runs in a web or extension page.
  anthropic: {
    base: (s) => s.baseUrl,
    unset: 'No Anthropic base URL configured',
    keyed: true,
    auth: (s) => ({ 'x-api-key': s.apiKey, 'anthropic-version': ANTHROPIC.version, 'anthropic-dangerous-direct-browser-access': 'true' }),
    body: (s, system, msgs) => ({
      model: s.model || ANTHROPIC_DEFAULTS.model,
      max_tokens: ANTHROPIC_DEFAULTS.maxTokens,
      ...(system ? { system } : {}),
      messages: msgs.map(anthropicMessage),
    }),
    reply: (r) => {
      const text = listOf(r?.content).map((b) => (b?.type === 'text' && typeof b.text === 'string' ? b.text : '')).join('');
      if (r?.stop_reason === 'max_tokens') throw LlmError.truncated(TRUNCATED_TEXT);
      if (r?.stop_reason === 'refusal') throw LlmError.refusal(text || 'The model refused this request');
      if (!Array.isArray(r?.content)) throw LlmError.badReply('malformed response (no content[] text)');
      return text;
    },
    failure: (s) => upstreamFailure(s.apiKey),
    init: (url) => keyedInit(url, isLoopbackHost),
    models: (v) => listOf(v.data).map((m) => m?.id),
    probe: (v) => ({ ok: true, detail: listOf(v.data)[0]?.id || '' }),
  },
});

// A provider's providers.json row and its wire strategy, or nulls for an unknown provider.
const wireFor = (provider) => {
  const info = Object.hasOwn(PROVIDER_INFO, provider ?? '') ? PROVIDER_INFO[provider] : null;
  return { info, wire: info ? WIRES[info.wire] : null };
};

// The endpoint these settings talk to (the stencil server's URL or the configured base); '' when unset.
export const providerUrl = (settings) => {
  const s = settings || {};
  const { wire } = wireFor(s.provider);
  return (wire ? wire.base(s) : s.baseUrl) || '';
};

// `getToken(serverUrl)` resolves the existing Stencil bearer token for stencil-server (sync or
// async); without one the surface's defaultGetToken applies (surface.js).
export const createLlmClient = ({ settings, fetchImpl = globalThis.fetch?.bind(globalThis), getToken } = {}) => {
  const s = settings || {};
  const resolveToken = getToken || defaultGetToken(s);

  // `signal` (optional AbortSignal) cancels the in-flight request — the panel's
  // Stop button rides on it; aborts surface as the runtime's AbortError.
  const chat = async ({ system, messages, signal }) => {
    if (s.provider === 'none') throw LlmError.config(ASSISTANT_OFF_TEXT);
    const { info, wire } = wireFor(s.provider);
    if (!wire) throw LlmError.http(`Unknown LLM provider "${s.provider}"`);
    const base = wire.base(s);
    if (!base) throw LlmError.http(wire.unset);
    if (wire.keyed && !s.apiKey) throw LlmError.disabled(NO_KEY_TEXT);
    const url = `${base}${info.chatPath}`;
    const init = wire.init?.(url);
    const headers = wire.auth(s, wire.session ? await resolveToken(s.serverUrl) : '');
    const r = await postJson(fetchImpl, url, wire.body(s, system, messages || []), headers, signal, wire.failure?.(s), init);
    return wire.reply(r);
  };

  return { chat };
};

// GET {serverUrl}/llm/info → { enabled, model } so the settings UI can render
// "via server X (model)". Errors propagate (the caller shows "unreachable").
export const fetchLlmInfo = async (serverUrl, { token = '', fetchImpl = globalThis.fetch?.bind(globalThis) } = {}) => {
  if (!fetchImpl) throw LlmError.http('no fetch implementation available');
  const resp = await fetchImpl(`${serverUrl}${PROVIDER_INFO['stencil-server'].infoPath}`, { headers: { Authorization: 'Bearer ' + token }, redirect: NO_REDIRECT });
  if (!resp.ok) throw LlmError.http(`HTTP ${resp.status}`);
  return readReply(resp);
};

// The wire's auth headers outside a chat, where an absent getToken means no token.
const infoHeaders = async (wire, s, getToken) =>
  wire.auth(s, wire.session && getToken ? await getToken(s.serverUrl) : '');

// Model suggestions for the settings datalist. Best-effort: NEVER throws, failures resolve [] —
// the Model field always stays free-form typing.
export const listModels = async (settings, { fetchImpl = globalThis.fetch?.bind(globalThis), getToken, timeoutMs = PROBE_TIMEOUT_MS } = {}) => {
  const s = settings || {};
  const { info, wire } = wireFor(s.provider);
  if (!fetchImpl || !wire || !wire.base(s) || (wire.keyed && !s.apiKey)) return [];
  try {
    const url = `${wire.base(s)}${info.modelsPath}`;
    const r = await getInfo(fetchImpl, url, await infoHeaders(wire, s, getToken), timeoutMs, wire.init?.(url));
    return r.ok ? wire.models(r.body).filter(Boolean) : [];
  } catch {
    return [];
  }
};

// Cheap reachability probe, no chat tokens spent. NEVER throws: failures resolve
// { ok: false, detail }. Short timeout so a status refresh cannot hang.
export const probeProvider = async (settings, { fetchImpl = globalThis.fetch?.bind(globalThis), getToken, timeoutMs = PROBE_TIMEOUT_MS } = {}) => {
  const s = settings || {};
  const { info, wire } = wireFor(s.provider);
  const url = providerUrl(s);
  const base = { provider: s.provider, url, model: s.model || '' };
  const fail = (detail) => ({ ...base, ok: false, detail });
  if (s.provider === 'none') return fail('assistant turned off');
  if (!url) return fail('not configured');
  if (!fetchImpl) return fail('no fetch implementation available');
  if (!wire) return fail(`unknown provider "${s.provider}"`);
  if (wire.keyed && !s.apiKey) return fail(NO_KEY_TEXT);
  try {
    const at = `${url}${info.probePath}`;
    const r = await getInfo(fetchImpl, at, await infoHeaders(wire, s, getToken), timeoutMs, wire.init?.(at));
    return r.ok ? { ...base, ...wire.probe(r.body) } : fail(`HTTP ${r.status}`);
  } catch (err) {
    return fail(err?.message || 'not reachable');
  }
};
