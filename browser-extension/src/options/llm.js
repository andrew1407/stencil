import { originPattern } from '../lib/stencil.js';
import { loadConnections } from '../lib/connection/connections.js';
import { icon } from '../lib/icons.js';
import { loadLlmSettings, saveLlmSettings, PROVIDER_BASE_URLS, LLM_SETTINGS_KEY, defaultSettings } from '../llm/settings.js';
import { listModels, providerUrl } from '../llm/client.js';
import { serverTokenFor } from '../llm/surface.js';
import { providerOptions, fillProviderUrls } from './providerOptions.js';
import { readSessionKey, setSessionKey, forgetSessionKey, SESSION_KEY_ITEM, SESSION_KEY_TTL_MS } from '../llm/sessionKey.js';

// chrome.storage key `llmSettings` (llm-contract.md §5/§8). ensureLlmHostPermission stays as
// a guard in case <all_urls> ever narrows.
const llmProviderEl = document.getElementById('llm-provider');
llmProviderEl.replaceChildren(...providerOptions().map(([id, label]) => new Option(label, id)));
fillProviderUrls(document);
const llmBaseUrlEl = document.getElementById('llm-baseurl');
const llmModelEl = document.getElementById('llm-model');
const llmApiKeyEl = document.getElementById('llm-apikey');
const llmServerUrlEl = document.getElementById('llm-serverurl');
const llmServerTokenEl = document.getElementById('llm-servertoken');
const llmShareTabsEl = document.getElementById('llm-sharetabs');
const llmStatusEl = document.getElementById('llm-status');
// The anthropic session-key rows close the endpoint rows; static text only, never the key.
const SESSION_ROWS_HTML = `<div id="llm-session-rows" hidden>
  <label class="f" for="llm-sessionkey">Anthropic API key</label>
  <input id="llm-sessionkey" type="password" autocomplete="off" spellcheck="false" placeholder="sk-ant-… (kept for this browser session only)">
  <div class="row"><span id="llm-sessionkey-status" class="help"></span><button id="llm-sessionkey-forget" type="button">Forget key</button></div>
  <div class="help">Your key goes straight from the extension to Anthropic — no Stencil server sees it. It is kept for this browser
  session only, in <code>chrome.storage.session</code>: closing the browser, reloading the extension or ${SESSION_KEY_TTL_MS / 3_600_000} hours
  forget it, and it is never saved. Save holds a newly typed key.</div>
</div>`;
document.getElementById('llm-base-rows').insertAdjacentHTML('beforeend', SESSION_ROWS_HTML);
const llmSessionRowsEl = document.getElementById('llm-session-rows');
const llmSessionKeyEl = document.getElementById('llm-sessionkey');
const llmSessionStatusEl = document.getElementById('llm-sessionkey-status');
const llmSessionForgetEl = document.getElementById('llm-sessionkey-forget');
// The openai-compat key's label, field and help: hidden by style, since label.f sets its display.
const llmOpenaiKeyEls = [document.querySelector('label[for="llm-apikey"]'), llmApiKeyEl, document.getElementById('llm-apikey-help')];
let llmStored = null;   // last-loaded settings, so switching providers restores saved URLs
// What a double-click resets them to (lib/control/dblReset.js).
const LLM_DEFAULTS = defaultSettings();
llmProviderEl.dataset.default = LLM_DEFAULTS.provider;
llmShareTabsEl.dataset.default = String(LLM_DEFAULTS.shareTabs === true);

const syncLlmRows = () => {
  const server = llmProviderEl.value === 'stencil-server';
  const off = llmProviderEl.value === 'none';
  document.getElementById('llm-base-rows').hidden = server || off;
  document.getElementById('llm-server-rows').hidden = !server;
  document.getElementById('llm-model-row').hidden = off;
  document.getElementById('llm-tabs-row').hidden = off;   // nothing is sent when off
  const anthropic = llmProviderEl.value === 'anthropic';
  for (const el of llmOpenaiKeyEls) if (el) el.style.display = anthropic ? 'none' : '';
  llmSessionRowsEl.hidden = !anthropic;
};

// The held anthropic key's local expiry (the weekday once it is another day); the field is never filled back.
const untilText = (ms, now = Date.now()) => {
  const at = new Date(ms);
  const time = at.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  return at.toDateString() === new Date(now).toDateString() ? time : `${at.toLocaleDateString([], { weekday: 'short' })} ${time}`;
};
const renderSessionKey = async () => {
  const held = await readSessionKey();
  llmSessionStatusEl.textContent = held ? `Key kept for this browser session until ${untilText(held.expiresAt)}.` : 'No key for this session.';
  llmSessionForgetEl.disabled = !held;
};

// The saved value when the saved provider matches, else the provider's default.
const refillLlmBaseUrl = () => {
  const p = llmProviderEl.value;
  llmBaseUrlEl.value = (llmStored && llmStored.provider === p && llmStored.baseUrl)
    ? llmStored.baseUrl
    : (PROVIDER_BASE_URLS[p] || '');
};

// Best-effort suggestions from the CURRENTLY EDITED fields; a request counter drops stale answers.
const llmModelListEl = document.getElementById('llm-model-list');
let llmModelsReq = 0;
const refreshLlmModels = async () => {
  const req = ++llmModelsReq;
  const provider = llmProviderEl.value;
  const typedKey = (llmSessionKeyEl.value || '').trim();
  const settings = {
    provider,
    baseUrl: (llmBaseUrlEl.value || '').trim(),
    // anthropic: the key being typed, else the one held for the session.
    apiKey: provider === 'anthropic' ? (typedKey || (await readSessionKey())?.key || '') : (llmApiKeyEl.value || '').trim(),
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
  llmSessionKeyEl.value = '';
  refillLlmBaseUrl();
  syncLlmRows();
  renderSessionKey();
  refreshLlmModels();
};

llmProviderEl.addEventListener('change', () => { refillLlmBaseUrl(); syncLlmRows(); refreshLlmModels(); });
llmBaseUrlEl.addEventListener('change', refreshLlmModels);
llmApiKeyEl.addEventListener('change', refreshLlmModels);
llmServerUrlEl.addEventListener('change', refreshLlmModels);
llmServerTokenEl.addEventListener('change', refreshLlmModels);
llmSessionKeyEl.addEventListener('change', refreshLlmModels);
llmSessionForgetEl.addEventListener('click', async () => {
  llmSessionKeyEl.value = '';
  await forgetSessionKey();
  renderSessionKey();
  refreshLlmModels();
});

// Covered origins pass silently; anything else is requested (needs this click's gesture).
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
  // A newly typed anthropic key is held for this browser session from now; it is never saved.
  const typedKey = (llmSessionKeyEl.value || '').trim();
  llmSessionKeyEl.value = '';
  const keyRefused = s.provider === 'anthropic' && typedKey && !(await setSessionKey(typedKey));
  llmStored = await saveLlmSettings(s);
  renderSessionKey();
  const active = providerUrl(s);
  const granted = active ? await ensureLlmHostPermission(active) : true;
  llmStatusEl.innerHTML = icon('check', { size: 13 }) + ' Saved';
  if (keyRefused) llmStatusEl.textContent = 'Saved — but the browser refused to keep the key for this session.';
  else if (!granted) llmStatusEl.textContent = 'Saved — but access to that origin was not granted, so calls to it will fail.';
  else setTimeout(() => { llmStatusEl.textContent = ''; }, 1500);
});

chrome.storage.onChanged.addListener((changes, area) => {
  if (area === 'local' && changes[LLM_SETTINGS_KEY]) loadLlmForm();
  if (area === 'session' && changes[SESSION_KEY_ITEM]) renderSessionKey();
});
loadLlmForm();
