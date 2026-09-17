// ── window.stencil's assistant half — llm / prompt / chat ────────────────────
// Settings share the gear dialog's store (llm-contract.md §5) and the chat members
// delegate to the panel's own scripting surface, so UI and scripting never diverge.
import { loadLlmSettings, saveLlmSettings, PROVIDERS, withProvider, URL_KEYS, isHttpUrl } from '../llm/llmSettings.js';
import {
  chatSide, setChatSide, applyChatSide, CHAT_SIDE_SWAPPED,
} from '../ui/chatLayoutPrefs.js';
import { publish, EVENTS } from '../eventBus/appBus.js';
import { str } from './coerce.js';

export const createAssistantApi = ({ app, guard }) => {
  let stencil;   // the frozen facade, handed over by setFacade after the guard

  // Validate + persist a partial LLM-settings update through the SAME store the
  // assistant's gear dialog uses (llmSettings.js), so UI and scripting stay in sync.
  const applyLlmSetup = (opts = {}) => {
    const cur = loadLlmSettings();
    let next = { ...cur };
    if (opts.provider != null) {
      const p = str(opts.provider).trim();
      if (!PROVIDERS.includes(p)) throw new Error(`Unknown LLM provider "${opts.provider}" — one of ${PROVIDERS.join(', ')}`);
      // Switching providers refills the default base URL unless the user overrode it
      // (withProvider — literally the settings modal's rule); an explicit baseUrl
      // below still wins.
      if (p !== cur.provider) next = withProvider(next, p);
    }
    for (const k of URL_KEYS) {
      if (opts[k] != null) {
        const v = str(opts[k]).trim();
        if (v && !isHttpUrl(v)) throw new Error(`${k} must be an http(s) URL`);
        next[k] = v;
      }
    }
    if (opts.model != null) next.model = str(opts.model).trim();
    if (opts.apiKey != null) next.apiKey = str(opts.apiKey);
    saveLlmSettings(next);
    // Same live-refresh signal the settings modal fires (panel re-probes status).
    publish(EVENTS.llmSettingsChanged);
    return next;
  };

  // The chat panel registers its scripting surface as app.chat when it wires.
  const chatPanel = () => {
    if (!app.chat) throw new Error('Chat panel not ready — the editor UI has not wired yet');
    return app.chat;
  };

  // "Swap message sides" (chatLayoutPrefs.js) is deliberately NOT persisted. Both
  // transcripts share the one preference, so restamp whichever is mounted.
  const applyChatSideEverywhere = (side) => {
    if (typeof document === 'undefined') return;
    applyChatSide(document.getElementById('chat-transcript'), side);
    applyChatSide(document.getElementById('ctx-assist-transcript'), side);
  };

  const api = {
    // ── AI assistant (LLM) ──
    // Settings mirror the gear dialog (llm-contract.md §5) and share its store, so UI and
    // scripting stay in sync. apiKey reads back as-is — the trust stance server tokens take.
    //   stencil.llm.setup({ provider: 'ollama', model: 'llama3.2-vision' })
    get llm() {
      return guard({
        get provider() { return loadLlmSettings().provider; },
        set provider(v) { applyLlmSetup({ provider: v }); },
        get baseUrl() { return loadLlmSettings().baseUrl; },
        set baseUrl(v) { applyLlmSetup({ baseUrl: v }); },
        get model() { return loadLlmSettings().model; },
        set model(v) { applyLlmSetup({ model: v }); },
        get apiKey() { return loadLlmSettings().apiKey; },
        set apiKey(v) { applyLlmSetup({ apiKey: v }); },
        get serverUrl() { return loadLlmSettings().serverUrl; },
        set serverUrl(v) { applyLlmSetup({ serverUrl: v }); },
        // Partial update in one call; unknown providers / non-http(s) URLs throw.
        setup(opts = {}) { applyLlmSetup(opts); return stencil; },
      });
    },
    // One assistant turn through the panel's pipeline — shared history, rendered in its
    // transcript. `images` attaches base64 data: URLs. Resolves { reply, warnings,
    // results:[{ label, dataUrl }] }; typed LlmErrors (truncated/refusal/…) reject.
    prompt(text, { images = [] } = {}) {
      return chatPanel().prompt(str(text), Array.isArray(images) ? images : [images]);
    },
    // Chat panel control — the same code paths as the panel's own buttons.
    get chat() {
      return guard({
        open() { chatPanel().open(); return stencil; },
        close() { chatPanel().close(); return stencil; },
        dock(mode) { chatPanel().dock(mode); return stencil; },
        get isOpen() { return !!app.chat && app.chat.isOpen(); },
        // The settled transcript (contract §12.1 display form): [{ role, text }]
        // copies — no raw model JSON, no error cards, no in-flight row.
        get history() { return chatPanel().history(); },
        // Stop the in-flight turn (the Stop button's path). True when a turn
        // was actually running.
        abort() { return chatPanel().abort(); },
        // Fresh conversation — the trash button's exact path (history, queued
        // attachments, transcript, and the §12 persisted copy). Throws mid-turn.
        clear() { chatPanel().clear(); return stencil; },
        get isSending() { return !!app.chat && app.chat.isSending; },
        // Which side user/assistant/error bubbles draw on. Scoped to THIS tab's
        // session: never persisted (chatLayoutPrefs.js), and this is the one way to
        // adjust it outside the panel's own menu item.
        get swapSides() { return chatSide() === CHAT_SIDE_SWAPPED; },
        set swapSides(v) {
          setChatSide(v ? CHAT_SIDE_SWAPPED : 'normal');
          applyChatSideEverywhere();
        },
        // Dictation into the panel's composer — the mic face (the "…" item, a
        // double-click or a hold on Send). Turning it on stops the hands-free voice chat.
        get voiceInput() { return !!app.chat && app.chat.voiceInput; },
        set voiceInput(v) { chatPanel().setVoiceInput(!!v); },
      });
    },
  };

  return { api, setFacade: (f) => { stencil = f; } };
};
