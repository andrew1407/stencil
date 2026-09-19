// ── LLM assistant settings (llm-contract.md §5) ────────────────────────
// Persisted provider configuration in the contract's shape: { provider, baseUrl, model,
// apiKey, serverUrl }. Every localStorage access is guarded so the leaf is inert in Node.
import PROVIDERS_ASSET from '../config/llm/providers.json' with { type: 'json' };
import { loadSavedServers } from '../net/connectionStore.js';
import { validateHttpUrl } from '../core/validation.js';

const LLM_SETTINGS_KEY = 'drawingApp_llmSettings';

const ls = () => (typeof localStorage !== 'undefined' ? localStorage : null);

// 'none' is a local-only value: assistant switched off, nothing configured or sent.
export const PROVIDERS = Object.freeze(['none', ...Object.keys(PROVIDERS_ASSET.providers)]);

// Provider → default base URL (providers.json, contract §5). stencil-server's is null there:
// it uses an already-configured collaboration server connection, so it pre-fills empty.
export const PROVIDER_BASE_URLS = Object.fromEntries(
  Object.entries(PROVIDERS_ASSET.providers).map(([id, p]) => [id, p.defaultBaseUrl || '']),
);

// Endpoint keys are http(s) ONLY. The gate itself is core/validation.js, so this module,
// the stencil.llm facade and every other caller share one rule.
export const URL_KEYS = Object.freeze(['baseUrl', 'serverUrl']);
export const isHttpUrl = (v) => validateHttpUrl(v).ok;

// THE provider-switch rule: pre-fill the provider's default base URL unless the user overrode
// it. The settings modal and the stencil.llm facade both apply this one helper.
export const withProvider = (settings, provider) => {
  const wasDefault = !settings.baseUrl || settings.baseUrl === PROVIDER_BASE_URLS[settings.provider];
  return {
    ...settings,
    provider,
    baseUrl: wasDefault ? (PROVIDER_BASE_URLS[provider] || '') : settings.baseUrl,
  };
};

// First-run defaults: the assistant ships OFF (§5) until the user picks a provider; serverUrl
// pre-fills the first saved Stencil connection. saveChats, the §12 opt-in, is OFF everywhere.
export const defaultSettings = () => ({
  provider: 'none',
  baseUrl: '',
  model: '',
  apiKey: '',
  serverUrl: loadSavedServers()[0]?.url || '',
  saveChats: false,
});

// Saved overrides merged over the defaults. Bad/missing data degrades to defaults.
export const loadLlmSettings = () => {
  const out = defaultSettings();
  try {
    const raw = ls()?.getItem(LLM_SETTINGS_KEY);
    const saved = raw ? JSON.parse(raw) : null;
    if (saved && typeof saved === 'object') {
      for (const k of ['provider', 'baseUrl', 'model', 'apiKey', 'serverUrl']) {
        if (typeof saved[k] !== 'string') continue;
        if (URL_KEYS.includes(k) && saved[k] && !isHttpUrl(saved[k])) continue;   // keep the default
        out[k] = saved[k];
      }
      if (typeof saved.saveChats === 'boolean') out.saveChats = saved.saveChats;
    }
  } catch {
    /* storage blocked / corrupt — fall back to the defaults */
  }
  if (!PROVIDERS.includes(out.provider)) out.provider = 'none';
  return out;
};

// stencil-server auth: the LIVE connection's bearer token, falling back to the saved one so
// the assistant works before or without an open connection.
export const serverBearerToken = (app, url) => app?.connections?.get(url)?.token
  || loadSavedServers().find((s) => s.url === url)?.token || '';

export const saveLlmSettings = (s) => {
  try {
    const slim = {
      provider: s.provider || 'ollama',
      baseUrl: s.baseUrl || '',
      model: s.model || '',
      apiKey: s.apiKey || '',
      serverUrl: s.serverUrl || '',
      saveChats: s.saveChats === true,
    };
    ls()?.setItem(LLM_SETTINGS_KEY, JSON.stringify(slim));
  } catch {
    /* storage blocked — settings live for this session only */
  }
};
