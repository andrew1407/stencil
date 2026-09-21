// The Assistant flyout's chat wiring. The flyout is not a list of .ctx-items: typing,
// sending and executing a plan leave the menu open. `host` is the menu's own state.
import { notify } from '../../utils.js';
import { attachVoiceDust } from '../dust/voiceDust.js';
import { loadLlmSettings, serverBearerToken } from '../../llm/llmSettings.js';
import { probeProvider } from '../../llm/llmClient.js';
import { MAX_ATTACHMENTS } from '../../llm/chatController.js';
import {
  sharedChatController, peekChatController, runLoggedChatTurn, closedTurnToast, queueAttachments,
  ATTACHMENT_CAP_NOTICE, cacheProbe, cachedProbe, probeStatusClass, chatLog, onChatLog,
  clearSharedConversation, requeueRowAttachments, chatTurnInFlight,
} from '../../llm/chatSession.js';
import {
  renderChatLog, chatAttachmentChips, wireInputSizer, wireChatSuggestions, wireChatMoreMenu,
  wireChatSideToggle, syncComposerControls, wireChatComposer, wireComposerVoice,
  notifyAttachmentsChanged, CHAT_ATTACHMENTS_EVENT, wireChatRowMenu, chatRowMenuOpen,
} from '../chat/chatView.js';
import { subscribe } from '../../eventBus/appBus.js';

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
  let voiceCtl = null;
  const updateControls = () => syncComposerControls({ sendBtn, attachBtn, input }, host.sending(),
    { attachFull: (peekChatController(app)?.attachments.length ?? 0) >= MAX_ATTACHMENTS,
      voice: voiceCtl?.state(), voiceSupported: !!app.voice?.supported });
  app.assistantFlyoutOpen = () => host.menuIsOpen() && flyout.classList.contains('ctx-sub-visible');

  // The same shared controller, so a file queued here rides the next turn from either surface.
  const renderAttachments = () => chatAttachmentChips(attachList, peekChatController(app));
  subscribe(CHAT_ATTACHMENTS_EVENT, renderAttachments);
  renderAttachments();

  // The gear opens the panel's settings modal and closes the menu first (a modal behind a
  // popup is unusable). Its dot mirrors the shared probe.
  const setDot = (probe) => { statusDot.className = `conn-status conn-status-${probeStatusClass(probe)}`; };
  const refreshDot = () => {
    const settings = loadLlmSettings();
    const known = cachedProbe(settings);
    if (known) { setDot(known); return; }
    setDot(null);
    probeProvider(settings, { getToken: (url) => serverBearerToken(app, url) })
      .then((probe) => { cacheProbe(settings, probe); setDot(probe); })
      .catch(() => setDot({ ok: false }));
  };
  gearBtn.addEventListener('click', () => {
    // Captured now: this popup is about to hide before the modal can measure it.
    const rect = gearBtn.getBoundingClientRect();
    host.closeMenu();
    document.getElementById('chat-settings-overlay')?.__stencilModal?.open(rect);
  });
  // Refresh the dot as the flyout is about to open (free when the panel already probed).
  item.addEventListener('mouseenter', () => { if (item.dataset.noSub !== '1') refreshDot(); });
  // Engaged = typing, resizing the composer, or a running turn: the hover-out timers must
  // not yank the chat away then (keepSubOpen consults this).
  let resizing = false;
  // The row menu floats on the body over the flyout, so the pointer reads as "left".
  flyout._keepOpen = () => host.sending() || resizing || chatRowMenuOpen() || flyout.contains(document.activeElement);

  // The flyout is a fixed-height column, so growing the input takes room from the
  // transcript; re-placed anyway (it can hit the viewport clamp), the root menu never is.
  wireInputSizer(document.getElementById('ctx-assist-sizer'), input, {
    hold: (on) => { resizing = on; },
    onDrag: () => { if (flyout.classList.contains('ctx-sub-visible')) host.positionSub(item, flyout); },
  });

  // The shared row log, exactly like the panel's.
  const paint = () => renderChatLog(transcript, chatLog(), {
    onConfigure: host.closeMenu,
    // An expired collaboration-server session is fixed in Connections; the menu closes for it too.
    onReconnect: () => { host.closeMenu(); document.getElementById('connect-btn')?.click(); },
    // §11: this surface renders the same shared log, so its choice cards must answer too.
    onAskSubmit: (answer) => { runTurn(answer).catch(() => { /* shown in the transcript */ }); },
    onRetry: (text) => {
      // Never on top of a running turn; the shared flag catches one started from the panel.
      if (host.sending() || chatTurnInFlight()) return;
      peekChatController(app)?.requeueLastTurnAttachments?.();
      runTurn(text).catch(() => { /* shown in the transcript */ });
    },
  });
  onChatLog(paint);
  paint();
  // Anything clicked in here may make the editor relayout: hold the scroll grace open.
  flyout.addEventListener('click', () => { host.bumpBusy(); });
  // Clicking the parent opens the flyout too and drops the caret into the input.
  item.addEventListener('click', (e) => {
    if (flyout.contains(e.target)) return;
    if (item.dataset.noSub === '1') {
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

  const runTurn = async (text) => {
    host.setSending(true);
    updateControls();
    await runLoggedChatTurn(sharedChatController(app), text, {
      settings: loadLlmSettings(),
      begin: (abort) => { turnAbort = abort; },
      onResult: (res) => {
        // The same balloon the panel builds; the click opens the docked panel, which holds the
        // same conversation.
        const toast = closedTurnToast(res);
        if (!host.menuIsOpen() && toast) notify(toast.text, toast.type, { onClick: () => app.chat?.open() });
      },
      cleanup: () => {
        host.setSending(false);
        // The plan's last relayout can still be settling.
        host.bumpBusy();
        turnAbort = null;
        updateControls();
        renderAttachments();
        notifyAttachmentsChanged();
      },
    });
  };

  // Escape bubbles out of the composer on purpose (the document listener closes the menu).
  wireChatMoreMenu('ctx-assist', document, { onOpen: () => {
    updateControls();
    const clr = document.getElementById('ctx-assist-clear');
    if (clr) clr.disabled = !!transcript.querySelector('.chat-empty');
  } });
  wireChatSideToggle('ctx-assist', transcript, document);
  // One controller, one log: both surfaces repaint.
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
        () => notify(ATTACHMENT_CAP_NOTICE, 'info'));
      renderAttachments();
      notifyAttachmentsChanged();
    },
    onInput: updateControls,
    voice: voiceHooks,
  });
  voiceCtl = wireComposerVoice({ prefix: 'ctx-assist', input, sendBtn, app, send, sync: updateControls });
  host.setOnMenuClose(() => voiceCtl?.setMode(false));
  updateControls();
  // The same delegated wiring as the panel, so a rebuilt empty state stays clickable.
  wireChatSuggestions(transcript, (prompt) => {
    input.value = prompt;
    updateControls();
    input.focus();
  });
  // The shared row menu (panel parity): Insert appends into this composer, Resend re-queues
  // the row's attachments and goes through runTurn.
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
