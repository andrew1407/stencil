// ── Lazy boot: wire handlers + settings the first time the section opens ────
// Everything that only matters once the chat is actually on screen — the composer's
// buttons, drop/paste-to-attach, the two menus beside this file — and the provider
// probe behind the … trigger's status dot and its rich tooltip.
import { icon } from '../../lib/icons.js';
import { loadLlmSettings, LLM_SETTINGS_KEY } from '../../llm/settings.js';
import { probeProvider, PROVIDER_LABELS } from '../../llm/client.js';
import { serverTokenFor } from '../../llm/surface.js';
import { loadConnections } from '../../lib/connection/connections.js';
import { wireDropTarget, isVideoFile } from '../../lib/chat/drop.js';
import { AUTO_DISMISS_MS } from '../../lib/chat/ui.js';
import { createChatStatusTip } from '../../lib/chat/statusTip.js';
import { wireComposerMenu } from './composerMenu.js';
import { wireMsgMenu } from './msgMenu.js';

export const createBoot = ({ sectionEl, transcriptEl, inputEl, sendBtn, clearBtn,
                             view, tray, send, state }) => {
  const { addPendingFile, addPendingUrl, syncClearBtn } = tray;
  const { addCard, clearConversation, msgMeta, showSuggestions } = view;

  // ── The … trigger's rich status tooltip (lib/statusTip.js — a themed table,
  // saying only what the dropdown's items don't: reachability). ──
  const gearTip = createChatStatusTip({
    doc: document,
    getAnchor: () => document.getElementById('chat-more-btn'),
    labels: PROVIDER_LABELS,
    win: window,
  });

  // ── Settings / provider line ───────────────────────────────────────────────
  const refreshSettings = async () => {
    state.llmSettings = await loadLlmSettings();
    const s = state.llmSettings;
    // Browser parity: reachability lives on the … trigger's dot, not a provider line.
    // Generation-guarded so a slow probe can't paint over a newer one.
    const dot = document.getElementById('chat-status-dot');
    const moreBtn = document.getElementById('chat-more-btn');
    if (!dot || !moreBtn) return;
    const gen = ++state.probeGen;
    dot.className = 'chat-status-dot';
    gearTip.setProbe(null);
    const probe = await probeProvider(s, {
      getToken: async (u) => serverTokenFor(u, { connections: await loadConnections(), settings: s }),
    });
    if (gen !== state.probeGen) return;
    dot.className = `chat-status-dot ${probe.ok ? 'ok' : 'bad'}`;
    gearTip.setProbe(probe);
  };

  const boot = () => {
    if (state.booted) return;
    state.booted = true;

    sendBtn.innerHTML = icon('send', { size: 16 });
    // NOT `addEventListener('click', send)`: the click Event would arrive as `preset`.
    sendBtn.addEventListener('click', () => send());
    clearBtn.addEventListener('click', clearConversation);
    // The … trigger is what stays visible; the items carry their own glyphs.
    const moreTrigger = document.getElementById('chat-more-btn');
    // insertAdjacentHTML, NOT innerHTML: the button already contains #chat-status-dot
    // (the reachability badge, browser parity) and innerHTML would delete it.
    moreTrigger.insertAdjacentHTML('afterbegin', icon('dots', { size: 16 }));
    for (const [id, name] of [['chat-attach-btn', 'image'], ['chat-clear', 'trash'],
                              ['chat-swap-sides', 'swap'], ['chat-open-options', 'gear']]) {
      const el = document.getElementById(id);
      if (el) el.insertAdjacentHTML('afterbegin', icon(name, { size: 14 }));
    }
    syncClearBtn();                       // boots disabled: an empty transcript has nothing to clear
    inputEl.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); send(); }
    });

    // Attach failures are transient notices, not a permanent record: they carry a ×
    // and clear themselves after a few seconds.
    const attachFailed = (label, err) =>
      addCard('x', `${label}: ${err.message}`, false, AUTO_DISMISS_MS);

    const queueFiles = async (files) => {
      for (const f of files) {
        try { await addPendingFile(f); }
        catch (err) { attachFailed(`Couldn’t attach ${f.name || 'the dropped file'}`, err); }
      }
    };

    wireComposerMenu({ transcriptEl, gearTip, queueFiles, state });
    wireMsgMenu({ transcriptEl, inputEl, msgMeta, send, state });

    // The COMPOSER is the drop target, not the whole section: over the transcript the drop belongs
    // to the page behind it (browser panel.js parity).
    const composerEl = sectionEl.querySelector('.chat-composer');
    const cue = document.createElement('div');
    cue.className = 'chat-drop-cue';
    cue.setAttribute('aria-hidden', 'true');
    const cueIcon = document.createElement('span');
    cueIcon.className = 'chat-drop-cue-icon';
    cueIcon.innerHTML = icon('image', { size: 16 });   // fixed glyph, no user data
    const cueText = document.createElement('span');
    cueText.textContent = 'Drop to attach';
    cue.append(cueIcon, cueText);
    composerEl.appendChild(cue);
    wireDropTarget(composerEl, {
      highlight: composerEl,
      onDrop: async (payload) => {
        try {
          if (payload.kind === 'files') await queueFiles(payload.files);
          else await addPendingUrl(payload.url);
        } catch (err) {
          attachFailed('Couldn’t attach the dropped item', err);
        }
      },
    });

    // Paste-to-attach (browser parity): an image/video on the clipboard queues as an
    // attachment; a plain-text paste falls through to the textarea untouched.
    sectionEl.addEventListener('paste', (e) => {
      const files = [...(e.clipboardData?.files || [])]
        .filter((f) => (f.type || '').startsWith('image/') || isVideoFile(f));
      if (!files.length) return;
      e.preventDefault();
      queueFiles(files);
    });

    // Settings edited in Options while this surface is open: refresh the provider
    // line (LLM settings, storage.local) and drop the settings cache (storage.sync).
    chrome.storage.onChanged.addListener((changes, area) => {
      if (area === 'sync') state.settingsPromise = null;
      if (area === 'local' && changes[LLM_SETTINGS_KEY]) refreshSettings();
    });

    refreshSettings().then(() => {
      showSuggestions();   // the drag-hint note rides the empty state (chips included)
    });
  };

  return boot;
};
