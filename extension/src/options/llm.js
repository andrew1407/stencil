// ── AI assistant (LLM) settings ─────────────────────────────────────────────
import { originPattern } from '../lib/stencil.js';
import { loadConnections } from '../lib/connections.js';
import { icon } from '../lib/icons.js';
import { loadLlmSettings, saveLlmSettings, PROVIDER_BASE_URLS, LLM_SETTINGS_KEY } from '../llm/llmSettings.js';
import { listModels } from '../llm/llmClient.js';
import { serverTokenFor } from '../llm/llmSurface.js';

// Persisted under the chrome.storage key `llmSettings` (llm-contract.md §5/§8). The
// base/server URL is default-refilled per provider but stays editable;
// ensureLlmHostPermission stays as a guard in case <all_urls> ever narrows.
const llmProviderEl = document.getElementById('llm-provider');
const llmBaseUrlEl = document.getElementById('llm-baseurl');
const llmModelEl = document.getElementById('llm-model');
const llmApiKeyEl = document.getElementById('llm-apikey');
const llmServerUrlEl = document.getElementById('llm-serverurl');
const llmServerTokenEl = document.getElementById('llm-servertoken');
const llmShareTabsEl = document.getElementById('llm-sharetabs');
const llmStatusEl = document.getElementById('llm-status');
let llmStored = null;   // last-loaded settings, so switching providers restores saved URLs

const syncLlmRows = () => {
  const server = llmProviderEl.value === 'stencil-server';
  const off = llmProviderEl.value === 'none';
  document.getElementById('llm-base-rows').hidden = server || off;
  document.getElementById('llm-server-rows').hidden = !server;
  document.getElementById('llm-model-row').hidden = off;
  document.getElementById('llm-tabs-row').hidden = off;   // nothing is sent when off
};

// Refill the base URL for the chosen provider: the saved value when the saved
// provider matches, else the provider's default. Editable afterwards.
const refillLlmBaseUrl = () => {
  const p = llmProviderEl.value;
  llmBaseUrlEl.value = (llmStored && llmStored.provider === p && llmStored.baseUrl)
    ? llmStored.baseUrl
    : (PROVIDER_BASE_URLS[p] || '');
};

// Model suggestions from the provider itself (browser/desktop parity): the datalist
// refills from the CURRENTLY EDITED fields, best-effort — failures leave it empty.
// A request counter drops stale async answers when the fields change mid-fetch.
const llmModelListEl = document.getElementById('llm-model-list');
let llmModelsReq = 0;
const refreshLlmModels = async () => {
  const req = ++llmModelsReq;
  const settings = {
    provider: llmProviderEl.value,
    baseUrl: (llmBaseUrlEl.value || '').trim(),
    apiKey: (llmApiKeyEl.value || '').trim(),
    serverUrl: (llmServerUrlEl.value || '').trim(),
    serverToken: (llmServerTokenEl.value || '').trim(),
  };
  const names = await listModels(settings, {
    getToken: async (u) => serverTokenFor(u, { connections: await loadConnections(), settings }),
  });
  if (req !== llmModelsReq) return;   // fields changed while fetching
  llmModelListEl.textContent = '';
  for (const n of names) {
    const opt = document.createElement('option');
    opt.value = n;
    llmModelListEl.appendChild(opt);
  }
};

const loadLlmForm = async () => {
  llmStored = await loadLlmSettings();
  llmProviderEl.value = llmStored.provider;
  llmModelEl.value = llmStored.model;
  llmApiKeyEl.value = llmStored.apiKey;
  llmServerUrlEl.value = llmStored.serverUrl;
  llmServerTokenEl.value = llmStored.serverToken;
  llmShareTabsEl.checked = llmStored.shareTabs === true;
  refillLlmBaseUrl();
  syncLlmRows();
  refreshLlmModels();
};

llmProviderEl.addEventListener('change', () => { refillLlmBaseUrl(); syncLlmRows(); refreshLlmModels(); });
llmBaseUrlEl.addEventListener('change', refreshLlmModels);
llmApiKeyEl.addEventListener('change', refreshLlmModels);
llmServerUrlEl.addEventListener('change', refreshLlmModels);
llmServerTokenEl.addEventListener('change', refreshLlmModels);

// Make sure the extension may fetch the configured origin: covered origins pass
// silently; anything else is requested from the user (needs this click's gesture).
const ensureLlmHostPermission = async (url) => {
  const pattern = originPattern(url);
  if (!pattern || !chrome.permissions) return true;
  try {
    if (await chrome.permissions.contains({ origins: [pattern] })) return true;
    return await chrome.permissions.request({ origins: [pattern] });
  } catch {
    return true;   // permissions API unavailable — the fetch itself will surface any block
  }
};

document.getElementById('llm-save').addEventListener('click', async () => {
  const s = {
    provider: llmProviderEl.value,
    baseUrl: (llmBaseUrlEl.value || '').trim(),
    model: (llmModelEl.value || '').trim(),
    apiKey: (llmApiKeyEl.value || '').trim(),
    serverUrl: (llmServerUrlEl.value || '').trim(),
    serverToken: (llmServerTokenEl.value || '').trim(),
    shareTabs: llmShareTabsEl.checked === true,
  };
  llmStored = await saveLlmSettings(s);
  const active = s.provider === 'stencil-server' ? s.serverUrl : s.baseUrl;
  const granted = active ? await ensureLlmHostPermission(active) : true;
  llmStatusEl.innerHTML = icon('check', { size: 13 }) + ' Saved';
  if (!granted) llmStatusEl.textContent = 'Saved — but access to that origin was not granted, so calls to it will fail.';
  else setTimeout(() => { llmStatusEl.textContent = ''; }, 1500);
});

// Another surface (or window) changed the assistant settings — reload the form.
chrome.storage.onChanged.addListener((changes, area) => {
  if (area === 'local' && changes[LLM_SETTINGS_KEY]) loadLlmForm();
});
loadLlmForm();
