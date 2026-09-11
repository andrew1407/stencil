// ── LLM assistant settings (llm-contract.md §5) ────────────────────────
// Persisted provider configuration in the contract's shape: { provider, baseUrl, model,
// apiKey, serverUrl }. Every localStorage access is guarded so the leaf is inert in Node.
import PROVIDERS_ASSET from '../config/llm/providers.json' with { type: 'json' };
import { loadSavedServers } from '../net/connectionStore.js';

const LLM_SETTINGS_KEY = 'drawingApp_llmSettings';

const ls = () => (typeof localStorage !== 'undefined' ? localStorage : null);

// 'none' is a local-only value: assistant switched off, nothing configured or sent.
export const PROVIDERS = ['none', ...Object.keys(PROVIDERS_ASSET.providers)];

// Provider → pre-filled default base URL (providers.json, contract §5 table).
// stencil-server's is null there — it uses an already-configured collaboration
// server connection instead, so it pre-fills empty.
export const PROVIDER_BASE_URLS = Object.fromEntries(
  Object.entries(PROVIDERS_ASSET.providers).map(([id, p]) => [id, p.defaultBaseUrl || '']),
);

// Endpoint keys are http(s) ONLY — the one scheme check, shared by loadLlmSettings
// below and the stencil.llm setup facade, so a poisoned store and a scripted setter
// can never aim the client at javascript:/file:/chrome-extension:.
export const URL_KEYS = ['baseUrl', 'serverUrl'];
export const isHttpUrl = (v) => /^https?:\/\//i.test(String(v == null ? '' : v));

// Switch `settings` to `provider`, pre-filling its default base URL unless the user
// overrode it. THE provider-switch rule: the settings modal and the stencil.llm facade
// both apply this one helper, so they cannot drift.
export const withProvider = (settings, provider) => {
  const wasDefault = !settings.baseUrl || settings.baseUrl === PROVIDER_BASE_URLS[settings.provider];
  return {
    ...settings,
    provider,
    baseUrl: wasDefault ? (PROVIDER_BASE_URLS[provider] || '') : settings.baseUrl,
  };
};

// First-run defaults: ollama on its standard local port, model empty (the user picks),
// serverUrl pre-filled with the FIRST saved Stencil server connection (empty when none).
// saveChats is the §12 chat-persistence opt-in — OFF by default, everywhere.
export const defaultSettings = () => ({
  provider: 'ollama',
  baseUrl: PROVIDER_BASE_URLS.ollama,
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
  if (!PROVIDERS.includes(out.provider)) out.provider = 'ollama';
  return out;
};

// stencil-server auth: the LIVE connection's bearer token, falling back to the saved
// one so the assistant works before/without an open connection. Shared by the chat
// panel and the settings modal.
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
