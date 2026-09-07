import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { icon } from './icons.js';
import { loadLlmSettings, saveLlmSettings, serverBearerToken, withProvider } from '../llm/llmSettings.js';
import { listModels, probeProvider } from '../llm/llmClient.js';
import { loadSavedServers } from '../net/connectionStore.js';
import { visibleChatMoreBtn } from './chatView.js';
import { enhanceSelect } from './customSelect.js';
import { loadVoiceSettings, saveVoiceSettings, VOICE_LANGUAGES, SILENCE_MS_MIN, SILENCE_MS_MAX } from '../llm/voiceSettings.js';

// ── Component: assistant (LLM) settings modal ───────────────────
// Provider + endpoint configuration for the chat panel (llm-contract.md §5),
// persisted as drawingApp_llmSettings. Modeled on connectModal's settings-modal
// pattern. Endpoints come ONLY from here (explicit user configuration) — never
// from fetched or scanned content.
export class StencilLlmSettingsModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('sparkle', { size: 18 })} Assistant</h2>
                <button class="app-modal-close btn-icon-text" id="chat-settings-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <div class="vs-section">Provider</div>
                <div class="vs-row vs-field"><label data-title="Which LLM endpoint the assistant talks to">Provider</label>
                    <select id="chat-provider">
                        <option value="none">None (turned off)</option>
                        <option value="ollama">Ollama</option>
                        <option value="openai-compat">OpenAI API (LM Studio, vLLM, …)</option>
                        <option value="stencil-server">Stencil server</option>
                    </select>
                </div>
                <div class="vs-row vs-field" id="chat-base-url-row"><label data-title="Provider base URL">Base URL</label>
                    <input type="text" id="chat-base-url" placeholder="http://localhost:11434">
                </div>
                <div class="vs-row vs-field" id="chat-model-row"><label data-title="Model name (empty = provider default) — pick a suggestion or type any name">Model</label>
                    <input type="text" id="chat-model" list="chat-model-list" placeholder="(provider default)">
                    <datalist id="chat-model-list"></datalist>
                </div>
                <div class="vs-row vs-field" id="chat-api-key-row"><label data-title="Only if your endpoint requires auth — sent as 'Authorization: Bearer <key>'. Local servers (LM Studio, llama.cpp) need none; hosted OpenAI-compatible services issue keys in their account dashboard.">API key</label>
                    <input type="password" id="chat-api-key" placeholder="(optional — most local servers need none)">
                </div>
                <div class="vs-row vs-field" id="chat-server-row"><label data-title="Which connected Stencil server proxies the LLM">Server</label>
                    <select id="chat-server-select"></select>
                </div>
                <div class="vs-row" id="chat-server-status-row">
                    <span class="conn-status conn-status-connecting" id="chat-settings-status-dot"></span>
                    <span class="chat-server-status" id="chat-server-status"></span>
                </div>
                <div class="vs-section">Chat history</div>
                <!-- Box FIRST, its label right beside it: a checkbox reads as one control, and
                     split across the row it was neither obviously a checkbox nor obviously tied
                     to that label. Same shape as the desktop's (llmSettingsForm.cpp). -->
                <div class="vs-row vs-checks">
                    <label class="vs-inline-check" for="chat-save-chats" data-title="Save the assistant conversation with the active project and restore it when the project is reopened. Text only, most recent 32 turns; incognito never saves.&#10;&#10;For a project on a server the transcript is stored with it, so everyone that project is shared with can read it. Local projects stay on this machine.">
                        <input type="checkbox" id="chat-save-chats"> Save chats with projects
                    </label>
                </div>
                <div class="vs-section">Voice input</div>
                <div class="vs-row vs-field"><label for="chat-voice-silence" data-title="How long a pause ends what you are saying and sends it — dictation in the chat and the hands-free voice chat both use it">Send after a pause of (ms)</label>
                    <span class="vs-ctrl"><input type="number" id="chat-voice-silence" min="${SILENCE_MS_MIN}" max="${SILENCE_MS_MAX}" step="100"></span></div>
                <div class="vs-row vs-field"><label for="chat-voice-lang" data-title="The language the speech recognizer listens for — Default is English; stencil.voiceInputLanguage takes any BCP-47 tag">Language</label>
                    <span class="vs-ctrl"><select id="chat-voice-lang">${VOICE_LANGUAGES.map(([v, label]) => `<option value="${v}">${label}</option>`).join('')}</select></span></div>
                <div class="chat-cors-note" id="chat-settings-note">
                    <div id="chat-save-chats-note">
                        Chats saved with a server project are <strong>readable by everyone
                        the project is shared with</strong>; local projects stay on this machine.
                    </div>
                    <div id="chat-cors-note">
                        Local providers must allow this app's origin — Ollama via
                        <code>OLLAMA_ORIGINS</code>, LM Studio via its &quot;enable CORS&quot; switch.
                    </div>
                </div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">The assistant only plans Stencil operations — endpoints come from this dialog alone.</span>
                <span class="chat-settings-actions">
                    <button id="chat-settings-cancel" class="btn-icon-text">${icon('x', { size: 14 })}<span>Cancel</span></button>
                    <button id="chat-settings-save" class="btn-icon-text primary">${icon('check', { size: 14 })}<span>Save</span></button>
                </span>
            </div>
        </div>
    `;
  }
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
      try { window.dispatchEvent(new Event('stencil:llm-settings-changed')); } catch { /* no DOM */ }
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
