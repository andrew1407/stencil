// ── The Assistant flyout's chat wiring ──────────────────────────
// Extracted from contextMenu.js. The flyout is NOT a list of .ctx-items, so none of the
// menu's "activate → closeMenu()" wiring applies: typing, sending, stopping and executing
// a plan all leave the menu (and the flyout) open. `host` is the menu's own state.
import { notify } from '../utils.js';
import { attachVoiceDust } from './voiceDust.js';
import { loadLlmSettings, serverBearerToken } from '../llm/llmSettings.js';
import { probeProvider } from '../llm/llmClient.js';
import { MAX_ATTACHMENTS } from '../llm/chatController.js';
import {
  sharedChatController, peekChatController, runLoggedChatTurn, closedTurnToast, queueAttachments,
  ATTACHMENT_CAP_NOTICE, cacheProbe, cachedProbe, probeStatusClass, chatLog, onChatLog,
  clearSharedConversation, requeueRowAttachments, chatTurnInFlight,
} from '../llm/chatSession.js';
import {
  renderChatLog, chatAttachmentChips, wireInputSizer, wireChatSuggestions, wireChatMoreMenu,
  wireChatSideToggle, syncComposerControls, wireChatComposer, wireComposerVoice,
  notifyAttachmentsChanged, CHAT_ATTACHMENTS_EVENT, wireChatRowMenu, chatRowMenuOpen,
} from './chatView.js';
import { subscribe } from '../bus/appBus.js';

export const wireCtxAssistantChat = (app, host, openChatPanel) => {
  const item = document.getElementById('ctx-assist-menu');
  const flyout = document.getElementById('ctx-assist-sub');
  const transcript = document.getElementById('ctx-assist-transcript');
  const input = document.getElementById('ctx-assist-input');
  const sendBtn = document.getElementById('ctx-assist-send');
  attachVoiceDust(sendBtn, () => sendBtn.classList.contains('chat-voice-listening'));
  const attachBtn = document.getElementById('ctx-assist-attach-btn');
  const attachInput = document.getElementById('ctx-assist-attach-input');
  const gearBtn = document.getElementById('ctx-assist-settings-btn');
  const statusDot = document.getElementById('ctx-assist-status-dot');
  const attachList = document.getElementById('ctx-assist-attachments');
  if (!item || !flyout || !transcript || !input || !sendBtn) return;

  let turnAbort = null;
  let voiceCtl = null;   // wireComposerVoice, below
  // The shared send ↔ Stop swap (the panel's contract), plus the mic face.
  const updateControls = () => syncComposerControls({ sendBtn, attachBtn, input }, host.sending(),
    { attachFull: (peekChatController(app)?.attachments.length ?? 0) >= MAX_ATTACHMENTS,
      voice: voiceCtl?.state(), voiceSupported: !!app.voice?.supported });
  // The hands-free voice chat asks whether ANY chat surface shows its rows.
  app.assistantFlyoutOpen = () => host.menuIsOpen() && flyout.classList.contains('ctx-sub-visible');

  // ── Attachments: feed the SAME shared controller, so a file queued here rides
  // the next turn from either surface (both rows repaint on the change event). ──
  const renderAttachments = () => chatAttachmentChips(attachList, peekChatController(app));
  subscribe(CHAT_ATTACHMENTS_EVENT, renderAttachments);
  renderAttachments();

  // ── Settings gear: opens the assistant settings modal through the panel's own
  // gear (one modal, one wiring) — and CLOSES the menu first, because a modal
  // behind a popup menu is unusable. Its dot mirrors the shared probe. ──
  const setDot = (probe) => { statusDot.className = `conn-status conn-status-${probeStatusClass(probe)}`; };
  const refreshDot = () => {
    const settings = loadLlmSettings();
    const known = cachedProbe(settings);
    if (known) { setDot(known); return; }
    setDot(null);   // amber while in flight
    probeProvider(settings, { getToken: (url) => serverBearerToken(app, url) })
      .then((probe) => { cacheProbe(settings, probe); setDot(probe); })
      .catch(() => setDot({ ok: false }));
  };
  gearBtn.addEventListener('click', () => {
    // Captured NOW: this popup (gearBtn included) is about to hide before the
    // settings modal can measure it, and the panel's OWN #chat-settings-btn is a
    // different button entirely — neither is the control that was just clicked.
    const rect = gearBtn.getBoundingClientRect();
    host.closeMenu();
    document.getElementById('chat-settings-overlay')?.__stencilModal?.open(rect);
  });
  // Refresh the dot when the flyout is about to open (free when the panel already
  // probed — see the shared cache). No dot to paint in plain mode.
  item.addEventListener('mouseenter', () => { if (item.dataset.noSub !== '1') refreshDot(); });
  // "Engaged" = typing in the flyout, resizing its composer, or a running turn —
  // the hover-out timers must not yank the chat away then (a SIBLING submenu
  // parent still closes it). keepSubOpen consults this predicate.
  let resizing = false;
  // The row menu floats on the body OVER the flyout, so while it shows the
  // pointer reads as "left" — that must not close the chat under it.
  flyout._keepOpen = () => host.sending() || resizing || chatRowMenuOpen() || flyout.contains(document.activeElement);

  // ── Resizable composer: the panel's slider strip. The flyout is a fixed-height
  // column, so growing the input takes room from the transcript; re-place it anyway
  // (its height can hit the viewport clamp) — the ROOT menu is never re-placed. ──
  wireInputSizer(document.getElementById('ctx-assist-sizer'), input, {
    hold: (on) => { resizing = on; },
    onDrag: () => { if (flyout.classList.contains('ctx-sub-visible')) host.positionSub(item, flyout); },
  });

  // ── Transcript: the SHARED row log, exactly like the panel's — both surfaces
  // render the same rows in the same order, and this one shows the history that
  // happened while the menu was closed. ──
  const paint = () => renderChatLog(transcript, chatLog(), {
    onConfigure: host.closeMenu,   // the CTA opens the settings modal; the menu must go
    // An expired collaboration-server session is fixed in Connections, not in the
    // provider settings — and the menu closes for that modal just the same.
    onReconnect: () => { host.closeMenu(); document.getElementById('connect-btn')?.click(); },
    // §11: this surface renders the SAME shared log, so its choice cards must answer
    // too — otherwise a card shown here would be inert while the panel's works.
    onAskSubmit: (answer) => { runTurn(answer).catch(() => { /* shown in the transcript */ }); },
    // Retry on a failed turn: the SAME text through the normal send path.
    onRetry: (text) => {
      // Never on top of a running turn — panel parity, and the shared flag catches
      // one started from the panel too (that is what logged the prompt twice).
      if (host.sending() || chatTurnInFlight()) return;
      // The failed turn's attachments ride the retry too (panel parity).
      peekChatController(app)?.requeueLastTurnAttachments?.();
      runTurn(text).catch(() => { /* shown in the transcript */ });
    },
  });
  onChatLog(paint);
  paint();
  // Anything clicked in here (send, a chip, "open as the working image") may make
  // the editor relayout — hold the scroll grace open across it.
  flyout.addEventListener('click', () => { host.bumpBusy(); });
  // Clicking the parent opens the flyout too (hover is the primary affordance,
  // like the other parents) and drops the caret into the input.
  item.addEventListener('click', (e) => {
    if (flyout.contains(e.target)) return;
    if (item.dataset.noSub === '1') {
      // Phones/touch: no flyout here — hand over to the (modal) chat panel.
      host.closeMenu();
      openChatPanel();
      return;
    }
    if (!flyout.classList.contains('ctx-sub-visible')) {
      host.positionSub(item, flyout);
      host.setActiveSub(flyout, item);
    }
    input.focus();
  });

  // The shared logged-turn frame; only the deltas below are this surface's own.
  const runTurn = async (text) => {
    host.setSending(true);
    updateControls();
    await runLoggedChatTurn(sharedChatController(app), text, {
      settings: loadLlmSettings(),
      begin: (abort) => { turnAbort = abort; },
      // A turn that lands after the menu was dismissed must not vanish silently.
      onResult: (res) => {
        // The SAME balloon the panel builds — and a way back: reopening this flyout
        // would need the menu at its old point, so the click opens the docked
        // panel, which holds the very same conversation.
        const toast = closedTurnToast(res);
        if (!host.menuIsOpen() && toast) notify(toast.text, toast.type, { onClick: () => app.chat?.open() });
      },
      cleanup: () => {
        host.setSending(false);
        // The plan's last relayout can still be settling — keep the grace window
        // open a moment past the turn.
        host.bumpBusy();
        turnAbort = null;
        updateControls();
        renderAttachments();          // the turn consumed the queue…
        notifyAttachmentsChanged();   // …so the panel's row drops them too
      },
    });
  };

  // Escape bubbles out of the composer on purpose (the document listener closes
  // the menu); every other key stays inside. Global hotkeys ignore typing targets.
  wireChatMoreMenu('ctx-assist', document, { onOpen: () => {
    updateControls();
    const clr = document.getElementById('ctx-assist-clear');
    if (clr) clr.disabled = !!transcript.querySelector('.chat-empty');
  } });   // attach / clear / settings behind the …
  wireChatSideToggle('ctx-assist', transcript, document);
  // Clear from THIS surface clears the shared conversation, exactly like the
  // panel's own item (one controller, one log — both surfaces repaint).
  document.getElementById('ctx-assist-clear')?.addEventListener('click', () => {
    if (host.sending()) return;
    clearSharedConversation(app);
  });
  const voiceHooks = {
    isOn: () => !!voiceCtl?.isOn(),
    isListening: () => !!voiceCtl?.isListening(),
    toggleMode: () => voiceCtl?.toggleMode(),
    toggleListening: () => voiceCtl?.toggleListening(),
  };
  const send = wireChatComposer({ input, sendBtn, attachBtn, attachInput }, {
    isSending: () => host.sending(),
    abort: () => turnAbort?.abort(),
    submit: (text) => { updateControls(); runTurn(text); },
    attachFiles: async (files) => {
      await queueAttachments(sharedChatController(app), files,
        (err) => notify(`Attachment failed — ${err.message}`, 'fail'),
        () => notify(ATTACHMENT_CAP_NOTICE, 'info'));   // the cap is a notice, not a failure
      renderAttachments();
      notifyAttachmentsChanged();
    },
    onInput: updateControls,
    voice: voiceHooks,
  });
  voiceCtl = wireComposerVoice({ prefix: 'ctx-assist', input, sendBtn, app, send, sync: updateControls });
  // A dismissed menu never keeps a mic open: the face goes back to Send too.
  host.setOnMenuClose(() => voiceCtl?.setMode(false));
  updateControls();
  // Suggestion chips prefill the input (editable before sending) — the same shared
  // delegated wiring as the panel, so a rebuilt empty state stays clickable.
  wireChatSuggestions(transcript, (prompt) => {
    input.value = prompt;
    updateControls();
    input.focus();
  });
  // Right-click on a transcript row: the SHARED row menu (panel parity).
  // Insert appends into THIS flyout's composer; Resend re-queues the row's
  // original attachments and re-sends through the same runTurn path.
  wireChatRowMenu(transcript, {
    onInsert: (text) => {
      input.value = input.value ? `${input.value}\n${text}` : text;
      updateControls();
      input.focus();
    },
    onResend: (text, attachments) => {
      if (host.sending() || chatTurnInFlight()) return;
      requeueRowAttachments(sharedChatController(app), attachments);
      renderAttachments();
      notifyAttachmentsChanged();
      runTurn(text).catch(() => { /* shown in the transcript */ });
    },
  });
  updateControls();
};
