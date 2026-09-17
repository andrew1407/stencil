import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { llmSettingsModalInner } from './llmSettingsMarkup.js';
import { icon } from './icons.js';
import { loadLlmSettings, saveLlmSettings, serverBearerToken, withProvider } from '../llm/llmSettings.js';
import { listModels, probeProvider } from '../llm/llmClient.js';
import { loadSavedServers } from '../net/connectionStore.js';
import { visibleChatMoreBtn } from './chatView.js';
import { enhanceSelect } from './customSelect.js';
import { loadVoiceSettings, saveVoiceSettings } from '../llm/voiceSettings.js';
import { publish, EVENTS } from '../eventBus/appBus.js';

// Provider + endpoint configuration (llm-contract.md §5), persisted as drawingApp_llmSettings.
// Endpoints come only from here — never from fetched or scanned content.
export class StencilLlmSettingsModal extends StencilElement {
  static inner() { return llmSettingsModalInner(); }
  static template() { return hostTag('stencil-llm-settings-modal', 'id="chat-settings-overlay" class="app-modal-overlay"', StencilLlmSettingsModal.inner()); }

  wire(app) {
    const $ = (id) => document.getElementById(id);
    const overlay = $('chat-settings-overlay');
    const providerEl = $('chat-provider');
    const baseUrlRow = $('chat-base-url-row');
    const baseUrlEl = $('chat-base-url');
    const modelEl = $('chat-model');
    const apiKeyRow = $('chat-api-key-row');
    const apiKeyEl = $('chat-api-key');
    const serverRow = $('chat-server-row');
    const serverEl = $('chat-server-select');
    const statusRow = $('chat-server-status-row');
    const statusEl = $('chat-server-status');
    const statusDot = $('chat-settings-status-dot');
    const corsNote = $('chat-cors-note');
    const saveChatsEl = $('chat-save-chats');
    const voiceSilenceEl = $('chat-voice-silence');
    const voiceLangEl = $('chat-voice-lang');
    // Our own list, not the OS's (ui/customSelect.js).
    enhanceSelect(voiceLangEl);

// The working copy: edits live here and reach storage only on Save; every close discards,
// since onOpen reloads from storage (the desktop dialog's commit/discard model).
    let settings = loadLlmSettings();
    let voice = loadVoiceSettings();
    const persist = () => {
      saveLlmSettings(settings);
      publish(EVENTS.llmSettingsChanged);
    };

    const serverUrls = () => {
      const urls = [];
      for (const u of app.connections?.urls || []) urls.push(u);
      for (const s of loadSavedServers()) if (!urls.includes(s.url)) urls.push(s.url);
      return urls;
    };

// The same probeProvider behind the composer gear's dot (desktop LlmSettingsForm parity).
// The generation counter drops stale async results.
    let statusReq = 0;
    const setStatus = (state, text) => {
      statusDot.className = `conn-status conn-status-${state}`;
      statusEl.textContent = text;
    };
    const renderStatus = async () => {
      const req = ++statusReq;
      if (settings.provider === 'none') {
        setStatus('error', 'Assistant turned off — nothing is sent anywhere.');
        return;
      }
      if (settings.provider === 'stencil-server' && !settings.serverUrl) {
        setStatus('error', 'No Stencil server configured — connect one first (Servers dialog).');
        return;
      }
      setStatus('connecting', 'Checking the configured LLM…');
      const probe = await probeProvider(settings, { getToken: (u) => serverBearerToken(app, u) });
      if (req !== statusReq) return;   // settings changed while probing
      if (probe.ok) {
        let text = probe.detail ? `Connected — ${probe.detail}` : 'Connected';
        if (settings.provider === 'stencil-server') text = `Connected — via ${probe.url} (${probe.detail || 'server default'})`;
        setStatus('connected', text);
      } else {
        setStatus('error', probe.detail || 'Unreachable');
      }
    };

    const render = () => {
      providerEl.value = settings.provider;
      baseUrlEl.value = settings.baseUrl;
      modelEl.value = settings.model;
      apiKeyEl.value = settings.apiKey;
      saveChatsEl.checked = settings.saveChats === true;
      voiceSilenceEl.value = voice.silenceMs;
      voiceLangEl.value = voice.language;
      const isServer = settings.provider === 'stencil-server';
      const isOff = settings.provider === 'none';
      baseUrlRow.style.display = isServer || isOff ? 'none' : '';
      $('chat-model-row').style.display = isOff ? 'none' : '';
      apiKeyRow.style.display = settings.provider === 'openai-compat' ? '' : 'none';
      serverRow.style.display = isServer ? '' : 'none';
      corsNote.style.display = isServer || isOff ? 'none' : '';
      if (isServer) {
        serverEl.innerHTML = '';
        const urls = serverUrls();
        if (!urls.length) {
          const opt = document.createElement('option');
          opt.value = '';
          opt.textContent = '(no servers configured)';
          serverEl.appendChild(opt);
        }
        for (const url of urls) {
          const opt = document.createElement('option');
          opt.value = url;
          opt.textContent = url;
          serverEl.appendChild(opt);
        }
        if (!settings.serverUrl && urls.length) settings.serverUrl = urls[0];
        serverEl.value = settings.serverUrl;
      }
      renderStatus();
      refreshModels();
    };

// The provider's own model list feeds the datalist; typing stays free-form.
    const modelList = $('chat-model-list');
    let modelReq = 0;
    const refreshModels = async () => {
      const req = ++modelReq;
      const names = await listModels(settings, { getToken: (u) => serverBearerToken(app, u) });
      if (req !== modelReq) return;   // settings changed while fetching
      modelList.textContent = '';
      for (const n of names) {
        const opt = document.createElement('option');
        opt.value = n;
        modelList.appendChild(opt);
      }
    };

    providerEl.addEventListener('change', () => {
// The provider's default base URL, unless the user overrode it (withProvider).
      settings = withProvider(settings, providerEl.value);
      render();
    });
    baseUrlEl.addEventListener('change', () => { settings.baseUrl = baseUrlEl.value.trim(); renderStatus(); refreshModels(); });
    modelEl.addEventListener('change', () => { settings.model = modelEl.value.trim();  });
    apiKeyEl.addEventListener('change', () => { settings.apiKey = apiKeyEl.value.trim(); renderStatus(); refreshModels(); });
    serverEl.addEventListener('change', () => { settings.serverUrl = serverEl.value; renderStatus(); refreshModels(); });
// Re-probe once typing settles (desktop's 600ms debounce), pre-persist.
    let typeTimer = null;
    const typeProbe = () => {
      clearTimeout(typeTimer);
      typeTimer = setTimeout(() => {
        settings.baseUrl = baseUrlEl.value.trim();
        settings.apiKey = apiKeyEl.value.trim();
        renderStatus();
      }, 600);
    };
    baseUrlEl.addEventListener('input', typeProbe);
    apiKeyEl.addEventListener('input', typeProbe);
// §12 opt-in: disabling stops writing but deletes nothing.
    saveChatsEl.addEventListener('change', () => { settings.saveChats = saveChatsEl.checked;  });

    const shell = wireModalShell(overlay, $('chat-settings-btn'), $('chat-settings-close'), {
// Reopening from what is stored is what makes every close a discard.
      onOpen: () => { settings = loadLlmSettings(); voice = loadVoiceSettings(); render(); },
// The gear sits inside the composer's "…" menu, which closes as it is clicked.
      originEl: () => visibleChatMoreBtn(),
    });
    // The only path that writes storage.
    $('chat-settings-save').addEventListener('click', () => {
      // Fields may hold text the user never blurred.
      settings.baseUrl = baseUrlEl.value.trim();
      settings.model = modelEl.value.trim();
      settings.apiKey = apiKeyEl.value.trim();
      settings.saveChats = saveChatsEl.checked;
      persist();
      saveVoiceSettings({ silenceMs: voiceSilenceEl.value, language: voiceLangEl.value });
      shell.close();
    });
    $('chat-settings-cancel').addEventListener('click', () => shell.close());
  }
}
define('stencil-llm-settings-modal', StencilLlmSettingsModal);
