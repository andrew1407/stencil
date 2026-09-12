import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { llmSettingsModalInner } from './llmSettingsMarkup.js';
import { icon } from './icons.js';
import { loadLlmSettings, saveLlmSettings, serverBearerToken, withProvider } from '../llm/llmSettings.js';
import { listModels, probeProvider } from '../llm/llmClient.js';
import { loadSavedServers } from '../net/connectionStore.js';
import { visibleChatMoreBtn } from './chatView.js';
import { enhanceSelect } from './customSelect.js';
import { loadVoiceSettings, saveVoiceSettings } from '../llm/voiceSettings.js';
import { publish, EVENTS } from '../bus/appBus.js';

// ── Component: assistant (LLM) settings modal ───────────────────
// Provider + endpoint configuration for the chat panel (llm-contract.md §5), persisted as
// drawingApp_llmSettings, on connectModal's settings-modal pattern. Endpoints come ONLY
// from here (explicit user configuration) — never from fetched or scanned content.
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
    // Our own list, not the OS's (ui/customSelect.js) — like the Visuals dialog's menus.
    enhanceSelect(voiceLangEl);

    // The WORKING copy: edits live here (so the status row probes what you are
    // typing) and reach storage only on Save — the desktop dialog's
    // commit/discard model. Closing by any means (Cancel, ×, Escape,
    // click-outside) discards, since onOpen reloads from storage.
    let settings = loadLlmSettings();
    let voice = loadVoiceSettings();   // its own store (drawingApp_voiceSettings), same commit model
    // Persist AND tell the chat panel so it re-probes the provider status live.
    const persist = () => {
      saveLlmSettings(settings);
      publish(EVENTS.llmSettingsChanged);
    };

    // Server dropdown: live connections first, then saved-but-closed servers.
    const serverUrls = () => {
      const urls = [];
      for (const u of app.connections?.urls || []) urls.push(u);
      for (const s of loadSavedServers()) if (!urls.includes(s.url)) urls.push(s.url);
      return urls;
    };

    // Live reachability of the EDITED settings — the same probeProvider behind
    // the composer gear's dot, so misconfiguration shows inside the modal too
    // (desktop LlmSettingsForm status-row parity). A generation counter drops
    // stale async results so rapid edits never paint an older probe.
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
      voiceLangEl.value = voice.language;   // the themed dropdown mirrors the wrapped setter
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

    // Model suggestions: the provider's own model list feeds the datalist (ollama
    // tags / openai-compat models / the server's default) — typing stays free-form.
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
      // Pre-fill the provider's default base URL on switch — unless the user has
      // overridden it (withProvider, the rule shared with stencil.llm()).
      settings = withProvider(settings, providerEl.value);
      render();
    });
    baseUrlEl.addEventListener('change', () => { settings.baseUrl = baseUrlEl.value.trim(); renderStatus(); refreshModels(); });
    modelEl.addEventListener('change', () => { settings.model = modelEl.value.trim();  });
    apiKeyEl.addEventListener('change', () => { settings.apiKey = apiKeyEl.value.trim(); renderStatus(); refreshModels(); });
    serverEl.addEventListener('change', () => { settings.serverUrl = serverEl.value; renderStatus(); refreshModels(); });
    // Re-probe once typing settles — no request per keystroke (desktop's 600ms
    // debounce), reading the LIVE field values so the check runs pre-persist.
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
    // §12 opt-in: enabling starts persisting from the next turn; disabling stops
    // writing but deletes nothing (clear/remove are the deletion paths).
    saveChatsEl.addEventListener('change', () => { settings.saveChats = saveChatsEl.checked;  });

    const shell = wireModalShell(overlay, $('chat-settings-btn'), $('chat-settings-close'), {
      // Reopening always starts from what is STORED — that is what makes every
      // close (Cancel, ×, Escape, click-outside) a discard.
      onOpen: () => { settings = loadLlmSettings(); voice = loadVoiceSettings(); render(); },
      // The gear sits inside the composer's "…" menu, which closes as it is clicked —
      // so the window flies to and from the "…" itself, and from above when no chat
      // surface is on screen to hold one.
      originEl: () => visibleChatMoreBtn(),
    });
    // Commit: the only path that writes storage and tells the panel to re-probe.
    $('chat-settings-save').addEventListener('click', () => {
      // The composer/model fields may hold text the user never blurred.
      settings.baseUrl = baseUrlEl.value.trim();
      settings.model = modelEl.value.trim();
      settings.apiKey = apiKeyEl.value.trim();
      settings.saveChats = saveChatsEl.checked;
      persist();
      saveVoiceSettings({ silenceMs: voiceSilenceEl.value, language: voiceLangEl.value });   // dispatches its own event
      shell.close();
    });
    $('chat-settings-cancel').addEventListener('click', () => shell.close());
  }
}
define('stencil-llm-settings-modal', StencilLlmSettingsModal);
