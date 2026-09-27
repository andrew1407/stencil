// ── LLM assistant settings (llm-contract.md §5 + §8) ───────────────────
// Persisted provider configuration in the contract's shape { provider, baseUrl, model,
// apiKey, serverUrl } plus the extension's own `serverToken`. Stored under the chrome.storage
// key `llmSettings`, in storage.local so the apiKey never syncs across machines; the anthropic
// key never lands here (sessionKey.js). All chrome.* access is guarded for `node --test`.
import PROVIDERS_ASSET from '../config/providers.json' with { type: 'json' };
import { loadConnections } from '../lib/connection/connections.js';
import { sessionKey } from './sessionKey.js';

export const LLM_SETTINGS_KEY = 'llmSettings';

// 'none' is a local-only value: assistant switched off, nothing configured or sent.
export const PROVIDERS = Object.freeze(['none', ...Object.keys(PROVIDERS_ASSET.providers)]);

// 'none' means OFF (contract §5): nothing is probed or sent and the assistant UI hides.
// Anything unrecognised counts as ON, matching loadLlmSettings' provider fallback.
export const assistantEnabled = (settings) => !!settings && settings.provider !== 'none';

// Provider → default base URL (providers.json, contract §5). stencil-server's is null there
// — it reuses a configured collaboration server connection, so it pre-fills empty.
export const PROVIDER_BASE_URLS = Object.fromEntries(
  Object.entries(PROVIDERS_ASSET.providers).map(([id, p]) => [id, p.defaultBaseUrl || '']),
);

// The string keys persisted/merged (contract §5 shape + the extension's serverToken).
const KEYS = ['provider', 'baseUrl', 'model', 'apiKey', 'serverUrl', 'serverToken'];

// Booleans, merged separately (typeof 'string' would drop them).
const BOOL_KEYS = ['shareTabs'];

// Endpoint keys are http(s) ONLY, so a poisoned chrome.storage entry can't aim the
// client at another scheme. Mirrors browser/js/llm/settings.js.
export const URL_KEYS = Object.freeze(['baseUrl', 'serverUrl']);
export const isHttpUrl = (v) => /^https?:\/\//i.test(String(v == null ? '' : v));

// First-run defaults; serverUrl pre-fills from the FIRST stored server connection.
// shareTabs (the §8 opt-in) is OFF: it would send other tabs' titles/URLs every turn.
export const defaultSettings = (connections = []) => ({
  provider: 'ollama',
  baseUrl: PROVIDER_BASE_URLS.ollama,
  model: '',
  apiKey: '',
  serverUrl: (Array.isArray(connections) && connections[0] && connections[0].url) || '',
  serverToken: '',
  shareTabs: false,
});

const storage = () => globalThis.chrome?.storage?.local;

// Bad or missing stored data degrades to the defaults. `connections` is injectable for
// tests; otherwise the stencil-server default URL follows the first configured server.
export const loadLlmSettings = async ({ connections } = {}) => {
  const conns = connections !== undefined ? connections : await loadConnections();
  const defaults = defaultSettings(conns);
  const out = { ...defaults };
  try {
    const o = await storage().get(LLM_SETTINGS_KEY);
    const saved = o ? o[LLM_SETTINGS_KEY] : null;
    if (saved && typeof saved === 'object') {
      for (const k of KEYS) {
        if (typeof saved[k] !== 'string') continue;
        if (URL_KEYS.includes(k) && saved[k] && !isHttpUrl(saved[k])) continue;   // keep the default
        out[k] = saved[k];
      }
      for (const k of BOOL_KEYS) {
        if (typeof saved[k] === 'boolean') out[k] = saved[k];
      }
    }
  } catch {
    /* storage unavailable (Node) / corrupt — fall back to the defaults */
  }
  if (!PROVIDERS.includes(out.provider)) out.provider = 'ollama';
  if (out.provider === 'anthropic') out.apiKey = '';
  // An empty saved serverUrl keeps following the first configured connection.
  if (!out.serverUrl) out.serverUrl = defaults.serverUrl;
  return out;
};

// What a request is built from: for anthropic the stored settings plus the session key, never
// kept or saved ('' when none is held, so the client sends nothing).
export const withSessionKey = async (settings) =>
  (settings?.provider === 'anthropic' ? { ...settings, apiKey: await sessionKey() } : settings);

export const saveLlmSettings = async (s = {}) => {
  const provider = PROVIDERS.includes(s.provider) ? s.provider : 'ollama';
  // The anthropic key is never stored, nor anything that is the held session key.
  const secret = provider === 'anthropic' || (s.apiKey && s.apiKey === await sessionKey());
  const slim = {
    provider,
    baseUrl: s.baseUrl || '',
    model: s.model || '',
    apiKey: secret ? '' : (s.apiKey || ''),
    serverUrl: s.serverUrl || '',
    serverToken: s.serverToken || '',
    // Explicit === true: anything short of a real opt-in stays off.
    shareTabs: s.shareTabs === true,
  };
  try { await storage().set({ [LLM_SETTINGS_KEY]: slim }); } catch { /* storage unavailable */ }
  return slim;
};
