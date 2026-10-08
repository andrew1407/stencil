import { StencilElement, hostTag, define } from '../base.js';
import { notify, PHONE_MEDIA } from '../../utils.js';
import { attachVoiceDust } from '../dust/voiceDust.js';
import {
  sharedChatController, peekChatController, queueAttachments, ATTACHMENT_CAP_NOTICE,
  chatLog, onChatLog, clearSharedConversation, requeueRowAttachments,
  chatTurnInFlight,
} from '../../llm/chat/session.js';
import { MAX_ATTACHMENTS } from '../../llm/chat/controller.js';
import { subscribe } from '../../eventBus/appBus.js';
import {
  renderChatLog, chatAttachmentChips, wireInputSizer, wireChatSuggestions,
  syncComposerControls, wireChatComposer, wireChatMoreMenu, wireComposerVoice,
  notifyAttachmentsChanged, CHAT_ATTACHMENTS_EVENT, wireChatRowMenu, wireChatSideToggle,
} from './view.js';
import { createChatStatusTip } from './statusTip.js';
import { chatPanelInner } from './panel/markup.js';
import { wireJumpPills } from './panel/jumpPills.js';
import { createPanelTurn } from './panel/turn.js';
import { wirePanelDrop } from './panel/drop.js';
import { wireOpenState } from './panel/openState.js';
import { createPanelApi } from './panel/api.js';
export {
  FLOAT_MIN_W, FLOAT_MIN_H, clampFloatRect, COMPACT_CHAT_W, COMPACT_CHAT_H, compactChatRect,
  resizeFloatRect, DOCK_ZONE_BAND, dockZoneAt, gearStatusRows, gearTipFootText,
} from './geometry.js';

// The AI assistant chat panel (llm-contract.md). Plans execute against the frozen
// window.stencil facade — the panel never edits pixels itself. Layout is session-only.

export class StencilChatPanel extends StencilElement {
  static inner() { return chatPanelInner(); }
// Sibling (not ancestor) backdrop, so a dock/float leaves the page clickable; CSS shows
// it only under the ≤680px modal shape.
  static template() {
    return `<div id="chat-backdrop"></div>`
      + hostTag('stencil-chat-panel', 'id="chat-panel" class="chat-panel" data-dust-scope=".chat-open:not(.chat-closing)"', StencilChatPanel.inner());
  }

  wire(app) {
    const $ = (id) => document.getElementById(id);
    const host = this;
    const transcript = $('chat-transcript');
    const attachList = $('chat-attachments');
    const input = $('chat-input');
    const sendBtn = $('chat-send');
    attachVoiceDust(sendBtn, () => sendBtn.classList.contains('chat-voice-listening'));

// The app's one controller (js/llm/chat/session.js), created lazily so it consumes the
// frozen window.stencil. The context-menu chat shares it.
    const ctrl = () => sharedChatController(app);

// A view of the shared row log, so both surfaces show the same messages.
    const paint = () => renderChatLog(transcript, chatLog(), {
// An expired collaboration-server session is fixed in Connections, not provider settings.
      onReconnect: () => document.getElementById('connect-btn')?.click(),
// §11: a choice card's answer is the user's next turn, through the same path as typing.
      onAskSubmit: (answer) => { runTurn(answer).catch(() => { /* rendered in the transcript */ }); },
// Guarded on the shared in-flight flag: a turn from the other surface is a turn too.
      onRetry: (text) => {
        if (turn.isSending || chatTurnInFlight()) return;
        peekChatController(app)?.requeueLastTurnAttachments?.();
        runTurn(text).catch(() => { /* rendered in the transcript */ });
      },
    });
    onChatLog(paint);
    paint();   // renders whatever the conversation already holds

    const { invalidatePillRects } = wireJumpPills({
      transcript, jumps: $('chat-jumps'), jumpPills: [$('chat-jump-top'), $('chat-jump-bottom')],
    });

// Provider status: the dot on the "…" trigger plus its tooltip (ui/chat/statusTip.js).
// The gear lives inside the menu and is hidden most of the time, so the "…" trigger hosts both.
    const { refreshStatus, hideGearTip } = createChatStatusTip({
      app, statusDot: $('chat-status-dot'), statusHost: $('chat-more-btn') || $('chat-settings-btn'),
    });

    // Attachments row: the context-menu composer paints the same queue.
    const renderAttachments = () => {
// peek, never create: the controller must not exist before window.stencil is frozen.
      chatAttachmentChips(attachList, peekChatController(app));
    };
    subscribe(CHAT_ATTACHMENTS_EVENT, renderAttachments);

    renderAttachments();

    const attachFiles = async (files) => {
      await queueAttachments(ctrl(), files, (err) => notify(`Attachment failed — ${err.message}`, 'fail'),
        () => notify(ATTACHMENT_CAP_NOTICE, 'info'));
      renderAttachments();
      notifyAttachmentsChanged();
    };
    const attachBtn = $('chat-attach-btn');

    let voiceCtl = null;
    const updateControls = () => {
      const queued = peekChatController(app)?.attachments.length ?? 0;
      syncComposerControls({ sendBtn, attachBtn, input }, turn.isSending,
        { attachFull: queued >= MAX_ATTACHMENTS, voice: voiceCtl?.state(), voiceSupported: !!app.voice?.supported });
      clearBtn.disabled = turn.isSending || !!transcript.querySelector('.chat-empty');
    };
// A turn landing while the panel is closed surfaces as a clickable toast. Open = visible
// and not mid-close (the closing slide keeps .chat-open).
    const panelIsOpen = () =>
      host.classList.contains('chat-open') && !host.classList.contains('chat-closing');
// setOpen and markChatBusy belong to the open state wired below, so they cross as thunks.
    const turn = createPanelTurn({
      transcript, ctrl, panelIsOpen, refreshStatus, renderAttachments, updateControls,
      setOpen: (on) => setOpen(on), markChatBusy: (on) => markChatBusy(on),
    });
    const { runTurn } = turn;

// Phone modal (components/chat/touch.css ≤680px) hides the drag sizer; the textarea
// auto-grows instead.
    const autoGrow = () => {
      if (typeof matchMedia === 'undefined' || !matchMedia(PHONE_MEDIA).matches) return;
      input.style.height = 'auto';
      input.style.height = `${input.scrollHeight + 2}px`;
    };

// Delegated on the transcript, so the chips still work after the block is rebuilt.
    wireChatSuggestions(transcript, (prompt) => {
      input.value = prompt;
      updateControls();
      input.focus();
    });

// The tip's 100003 tier sits over the menu's, so one left showing buries the menu.
    wireChatMoreMenu('chat', document, { onOpen: () => { updateControls(); hideGearTip(); } });
    wireChatSideToggle('chat', transcript, document);
// Dictation shares the send path: `send` is the very closure Enter uses.
    const voiceHooks = {
      isOn: () => !!voiceCtl?.isOn(),
      isListening: () => !!voiceCtl?.isListening(),
      toggleMode: () => voiceCtl?.toggleMode(),
      toggleListening: () => voiceCtl?.toggleListening(),
    };
    const send = wireChatComposer({ input, sendBtn, attachBtn, attachInput: $('chat-attach-input') }, {
      isSending: () => turn.isSending,
      abort: () => turn.abort(),
      submit: (text) => {
        updateControls();
        autoGrow();
        runTurn(text).catch(() => { /* rendered in the transcript */ });
      },
      attachFiles,
      onInput: () => { updateControls(); autoGrow(); },
      voice: voiceHooks,
    });
    voiceCtl = wireComposerVoice({ prefix: 'chat', input, sendBtn, app, send, sync: updateControls });
// The shared row menu: Insert appends into this composer; Resend re-queues the row's
// attachments and goes through runTurn.
    wireChatRowMenu(transcript, {
      onInsert: (text) => {
        input.value = input.value ? `${input.value}\n${text}` : text;
        updateControls();
        autoGrow();
        input.focus();
      },
      onResend: (text, attachments) => {
        if (turn.isSending || chatTurnInFlight()) return;
        requeueRowAttachments(ctrl(), attachments);
        renderAttachments();
        notifyAttachmentsChanged();
        runTurn(text).catch(() => { /* rendered in the transcript */ });
      },
    });

// A slider-style strip above the input row (the textarea is bottom-anchored, so only the
// top edge can move). Session-only.
    const inputSizer = $('chat-input-sizer');
    wireInputSizer(inputSizer, input, { host });

    const dropRow = $('chat-input-wrap');
    wirePanelDrop({ host, dropRow, attachFiles });

    const openBtn = $('chat-btn');
    const { setOpen, markChatBusy, chatDock, setDock, adoptLayout, openFrom } = wireOpenState({
      host, input, openBtn, panelIsOpen, refreshStatus, invalidatePillRects,
      resizer: $('chat-resizer'), header: $('chat-header'), backdrop: $('chat-backdrop'), closeBtn: $('chat-close'),
    });

// Clear = a fresh conversation; settings and the working image are untouched.
    const clearBtn = $('chat-clear');
    clearBtn.addEventListener('click', () => {
      if (turn.isSending || chatTurnInFlight()) return;
      clearSharedConversation(app);
      updateControls();
      input.focus();
    });


    chatDock.wireGestures();
    setDock(chatDock.mode());
    updateControls();

// Scripting surface (console/stencilApi.js): the same code paths as the buttons.
// `app.chat` is this panel; §12 persistence wires later under `app.chatPersistence`.
    app.chat = createPanelApi({
      app, ctrl, turn, setOpen, panelIsOpen, adoptLayout, setDock, renderAttachments, updateControls,
      floatAt: chatDock.floatAt, openFrom, voice: () => voiceCtl,
    });
  }
}
define('stencil-chat-panel', StencilChatPanel);
