import { StencilElement, hostTag, define } from '../base.js';
import { wireModalOpenGestures } from '../popover.js';
import { notify, PHONE_MEDIA, onWindowResize } from '../../utils.js';
import { icon } from '../icons.js';
import { attachVoiceDust } from '../dust/voiceDust.js';
import { loadLlmSettings } from '../../llm/llmSettings.js';
import {
  sharedChatController, peekChatController, runLoggedChatTurn, closedTurnToast, queueAttachments, ATTACHMENT_CAP_NOTICE,
  chatLog, onChatLog, clearSharedConversation, requeueRowAttachments,
  chatTurnInFlight,
} from '../../llm/chatSession.js';
import { rowsToMessages } from '../../llm/chatStore.js';
import { MAX_ATTACHMENTS } from '../../llm/chatController.js';
import { mediaFilesFromData, extractDraggedImageUrl, fetchDraggedMediaFile } from '../../core/pointer/dragImageUrl.js';
import { subscribe, EVENTS } from '../../eventBus/appBus.js';
import { modalShells } from '../modal/modalRegistry.js';
import { surfaceIn, surfaceOut, settleSurface, dockAwayPoint, motionReduced, rectCenter } from '../motion.js';
import {
  renderChatLog, stickToBottom, chatAttachmentChips, wireInputSizer, wireChatSuggestions, chatSuggestionsHtml,
  chatDropCueHtml, chatComposerActionsHtml, syncComposerControls, wireChatComposer, wireChatMoreMenu, wireComposerVoice,
  notifyAttachmentsChanged, CHAT_ATTACHMENTS_EVENT, wireChatRowMenu, rowMenuLiftPx, rowMenuLiftFits, chatPopupOpen,
  CHAT_POPUP_EVENT, wireChatSideToggle,
} from './chatView.js';

import {
  DOCKS, FLOAT_MIN_W, FLOAT_MIN_H, clampFloatRect, COMPACT_CHAT_W, COMPACT_CHAT_H, compactChatRect,
  resizeFloatRect, DOCK_ZONE_BAND, dockZoneAt, gearStatusRows, gearTipFootText,
} from './chatGeometry.js';
import { createChatDock } from './chatDock.js';
import { createChatStatusTip } from './chatStatusTip.js';
export {
  FLOAT_MIN_W, FLOAT_MIN_H, clampFloatRect, COMPACT_CHAT_W, COMPACT_CHAT_H, compactChatRect,
  resizeFloatRect, DOCK_ZONE_BAND, dockZoneAt, gearStatusRows, gearTipFootText,
} from './chatGeometry.js';

// The AI assistant chat panel (llm-contract.md). Plans execute against the frozen
// window.stencil facade — the panel never edits pixels itself. Layout is session-only.

export class StencilChatPanel extends StencilElement {
  static inner() {
    return `
        <div class="chat-header" id="chat-header">
            <span class="chat-title" id="chat-title">${icon('sparkle', { size: 14 })}<span class="chat-title-text">Assistant</span></span>
            <span class="chat-dock-btns">
                <button id="chat-dock-left-btn" class="chat-hbtn chat-dock-btn" data-dock="left" data-title="Dock left — or drag the header to an edge">${icon('chevron-left', { size: 13 })}</button>
                <button id="chat-dock-top-btn" class="chat-hbtn chat-dock-btn" data-dock="top" data-title="Dock top — or drag the header to an edge">${icon('chevron-up', { size: 13 })}</button>
                <button id="chat-dock-bottom-btn" class="chat-hbtn chat-dock-btn" data-dock="bottom" data-title="Dock bottom — or drag the header to an edge">${icon('chevron-down', { size: 13 })}</button>
                <button id="chat-dock-right-btn" class="chat-hbtn chat-dock-btn" data-dock="right" data-title="Dock right — or drag the header to an edge">${icon('chevron-right', { size: 13 })}</button>
                <button id="chat-float-btn" class="chat-hbtn chat-dock-btn" data-dock="float" data-title="Float — drag the header to move">${icon('maximize', { size: 13 })}</button>
            </span>
            <button id="chat-close" class="chat-hbtn" data-title="Close assistant">${icon('x', { size: 13 })}</button>
        </div>
        <div class="chat-transcript" id="chat-transcript">
            <div class="chat-empty" id="chat-empty">
                ${chatSuggestionsHtml()}
            </div>
        </div>
        <div class="chat-jumps" id="chat-jumps">
            <button id="chat-jump-top" class="chat-jump-btn" data-title="Jump to the beginning">${icon('chevron-up', { size: 14 })}</button>
            <button id="chat-jump-bottom" class="chat-jump-btn" data-title="Jump to the latest message">${icon('chevron-down', { size: 14 })}</button>
        </div>
        <div class="chat-attachments" id="chat-attachments"></div>
        <div class="chat-input-sizer" id="chat-input-sizer"></div>
        <div class="chat-input-row" id="chat-input-row">
            <div class="chat-input-wrap" id="chat-input-wrap">
                ${chatDropCueHtml()}
                <textarea id="chat-input" rows="2" placeholder="Ask the assistant… (Enter sends, Shift+Enter newline)"></textarea>
            </div>
            ${chatComposerActionsHtml({
    prefix: 'chat',
    actionsClass: 'chat-input-actions',
    gearClass: 'chat-config-btn',
    // Attach, Clear and Settings all live in the shared "…" menu now.
  })}
        </div>
        <div class="chat-resizer" id="chat-resizer"></div>
        <div class="chat-float-handle chat-float-handle-n"  data-dir="n"></div>
        <div class="chat-float-handle chat-float-handle-s"  data-dir="s"></div>
        <div class="chat-float-handle chat-float-handle-e"  data-dir="e"></div>
        <div class="chat-float-handle chat-float-handle-w"  data-dir="w"></div>
        <div class="chat-float-handle chat-float-handle-ne" data-dir="ne"></div>
        <div class="chat-float-handle chat-float-handle-nw" data-dir="nw"></div>
        <div class="chat-float-handle chat-float-handle-sw" data-dir="sw"></div>
    `;
  }
// Sibling (not ancestor) backdrop, so a dock/float leaves the page clickable; CSS shows
// it only under the ≤680px modal shape.
  static template() {
    return `<div id="chat-backdrop"></div>`
      + hostTag('stencil-chat-panel', 'id="chat-panel" class="chat-panel"', StencilChatPanel.inner());
  }

  wire(app) {
    const $ = (id) => document.getElementById(id);
    const host = this;
    const transcript = $('chat-transcript');
    const attachList = $('chat-attachments');
    const input = $('chat-input');
    const sendBtn = $('chat-send');
    attachVoiceDust(sendBtn, () => sendBtn.classList.contains('chat-voice-listening'));

// The app's one controller (js/llm/chatSession.js), created lazily so it consumes the
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
        if (sending || chatTurnInFlight()) return;
        peekChatController(app)?.requeueLastTurnAttachments?.();
        runTurn(text).catch(() => { /* rendered in the transcript */ });
      },
    });
    onChatLog(paint);
    paint();   // renders whatever the conversation already holds

// Jump pills over the transcript's bottom edge: ⌄ once scrolled up from the latest, ⌃
// once past the beginning (desktop chatDock parity).
    const jumps = $('chat-jumps');
    const jumpPills = [$('chat-jump-top'), $('chat-jump-bottom')];
// The hovered row's "…" yields to the pills: lifts clear, or hides when the bubble is
// too short (desktop placeChatCardMore's "shift, else hide").
    let hoverRow = null;
    const clearRowMenuLift = (row) => {
      row?.style.removeProperty('--row-menu-lift');
      row?.classList.remove('chat-row-menu-yield');
    };
// The pills sit outside the scroller, so their boxes are cached; invalidated when they
// show/hide, the window resizes, or the panel changes shape.
    let pillRects = null;
    const invalidatePillRects = () => { pillRects = null; };
    onWindowResize(invalidatePillRects);
    const syncRowMenuLift = () => {
      if (!hoverRow) return;
      const btn = hoverRow.querySelector('.chat-row-menu-btn')?.getBoundingClientRect?.();
      if (!btn) return;
// A hidden pill measures 0×0 and is filtered out, so this self-clears.
      const pills = pillRects ?? (pillRects = jumpPills
        .map((b) => b?.getBoundingClientRect?.()).filter((r) => r && r.width > 0));
      const lift = rowMenuLiftPx(btn, pills);
      if (!lift) { clearRowMenuLift(hoverRow); return; }
      if (rowMenuLiftFits(hoverRow.getBoundingClientRect(), btn, lift)) {
        hoverRow.style.setProperty('--row-menu-lift', `${lift}px`);
        hoverRow.classList.remove('chat-row-menu-yield');
      } else {
        hoverRow.style.removeProperty('--row-menu-lift');
        hoverRow.classList.add('chat-row-menu-yield');
      }
    };
    const syncJumps = () => {
      const max = transcript.scrollHeight - transcript.clientHeight;
// Any chat popup opens into this very corner.
      const standDown = chatPopupOpen();
      const up = transcript.scrollTop > 12 && !standDown;
      const down = max - transcript.scrollTop > 12 && !standDown;
      if (up !== jumps.classList.contains('can-up') || down !== jumps.classList.contains('can-down'))
        invalidatePillRects();
      jumps.classList.toggle('can-up', up);
      jumps.classList.toggle('can-down', down);
      syncRowMenuLift();
    };
// The pills float over the transcript, so hovering one leaves no row hovered.
    transcript.addEventListener('mouseover', (e) => {
      const row = e.target?.closest?.('.chat-msg');
// A lifted trigger sits outside its row's box, so reaching it crosses bare background;
// only a different row (or mouseleave) changes the hover.
      if (!row || !transcript.contains(row) || row === hoverRow) return;
      if (hoverRow) clearRowMenuLift(hoverRow);
      hoverRow = row;
      syncJumps();
    });
    transcript.addEventListener('mouseleave', () => {
      if (hoverRow) clearRowMenuLift(hoverRow);
      hoverRow = null;
      syncJumps();
    });
// Both edges of any chat popup, from either surface.
    subscribe(CHAT_POPUP_EVENT, syncJumps);
    transcript.addEventListener('scroll', syncJumps, { passive: true });
    new MutationObserver(syncJumps).observe(transcript, { childList: true, subtree: true });
    jumpPills[0].addEventListener('click', () => transcript.scrollTo({ top: 0, behavior: 'smooth' }));
    jumpPills[1].addEventListener('click', () => transcript.scrollTo({ top: transcript.scrollHeight, behavior: 'smooth' }));

// Provider status: the dot on the "…" trigger plus its tooltip (ui/chatStatusTip.js).
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

// While a turn is in flight the send button becomes Stop and attach pauses; one turn at
// a time — send() and the facade both guard on `sending`.
    let sending = false;
    let turnAbort = null;
    let voiceCtl = null;
    const updateControls = () => {
      const queued = peekChatController(app)?.attachments.length ?? 0;
      syncComposerControls({ sendBtn, attachBtn, input }, sending,
        { attachFull: queued >= MAX_ATTACHMENTS, voice: voiceCtl?.state(), voiceSupported: !!app.voice?.supported });
      clearBtn.disabled = sending || !!transcript.querySelector('.chat-empty');
    };

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

// A turn landing while the panel is closed surfaces as a clickable toast. Open = visible
// and not mid-close (the closing slide keeps .chat-open).
    const panelIsOpen = () =>
      host.classList.contains('chat-open') && !host.classList.contains('chat-closing');
    const closedToast = (res) => {
      const toast = closedTurnToast(res);
      if (panelIsOpen() || !toast) return;
      notify(toast.text, toast.type, { onClick: () => setOpen(true) });
    };

// The shared logged-turn frame; runTurn rethrows a failed turn so stencil.prompt gets
// the typed rejection too.
    const runTurn = async (text) => {
      sending = true;
      updateControls();
      markChatBusy(!panelIsOpen());
      const res = await runLoggedChatTurn(ctrl(), text, {
        settings: loadLlmSettings(),
// Sending is an explicit "take me to the newest": pin even if the user had scrolled up.
        begin: (abort) => { turnAbort = abort; stickToBottom(transcript); },
        onResult: (r) => {
          if (!r.ok && r.kind === 'unreachable') refreshStatus();
          closedToast(r);
        },
        cleanup: () => {
          sending = false;
          markChatBusy(false);
          turnAbort = null;
          renderAttachments();
          notifyAttachmentsChanged();
          updateControls();
          stickToBottom(transcript);
        },
      });
      if (!res.ok) throw res.error;
      return res.entry;
    };
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
      isSending: () => sending,
      abort: () => turnAbort?.abort(),
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
        if (sending || chatTurnInFlight()) return;
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

// Paste + drop on the panel: image/video files become attachments (mediaFilesFromData,
// the global wiring's extraction); text pastes stay native.
    host.addEventListener('paste', async (e) => {
      if (!host.classList.contains('chat-open')) return;
      const files = mediaFilesFromData(e.clipboardData);
      if (!files.length) return;
      e.preventDefault();
      e.stopPropagation();
      await attachFiles(files);
    });
// The composer acts on a drop, but the whole panel swallows one: falling through popped
// the canvas's "Open dropped image" dialog. A miss says where to aim.
    host.addEventListener('dragover', (e) => {
      if (!host.classList.contains('chat-open')) return;
      e.preventDefault();
      // …and it must still bubble: the document's drop-owner branch hides the global overlay.
      try { e.dataTransfer.dropEffect = 'copy'; } catch { /* older DnD */ }
    });
    host.addEventListener('drop', (e) => {
      if (!host.classList.contains('chat-open')) return;
// The composer's own handler runs first and stops propagation.
      e.preventDefault();
      e.stopPropagation();
      if (mediaFilesFromData(e.dataTransfer).length
          || extractDraggedImageUrl((t) => e.dataTransfer.getData(t))) {
        notify('Drop it on the message box to attach it', 'info');
      }
    });
// The text box is the drop target, not the whole row (desktop showDropCue parity).
    const dropRow = $('chat-input-wrap');
    dropRow.addEventListener('dragover', (e) => {
      if (!host.classList.contains('chat-open')) return;
      e.preventDefault();
// Must bubble to the document dragover: [data-drop-owner] tells it this row handles its own drops.
      try { e.dataTransfer.dropEffect = 'copy'; } catch { /* older DnD */ }
      dropRow.classList.add('chat-drop-target');
    });
    dropRow.addEventListener('dragleave', (e) => {
      if (!dropRow.contains(e.relatedTarget)) dropRow.classList.remove('chat-drop-target');
    });
    dropRow.addEventListener('drop', async (e) => {
      dropRow.classList.remove('chat-drop-target');
      if (!host.classList.contains('chat-open')) return;
      e.preventDefault();
      e.stopPropagation();
      const files = mediaFilesFromData(e.dataTransfer);
      if (files.length) { await attachFiles(files); return; }
// An image dragged from another page carries no File, just a URL in uri-list/html.
      const url = extractDraggedImageUrl((t) => e.dataTransfer.getData(t));
      if (!url) { notify('Nothing to attach from that drop', 'fail'); return; }
      try {
        await attachFiles([await fetchDraggedMediaFile(url, { accept: /^(image|video)\// })]);
      } catch (err) {
        notify(`Couldn't attach that image — ${err.message}`, 'fail');
      }
    });

    let gestures = null;
// ui/chatDock.js; playDust and the pill rects are declared below, so they cross as thunks.
    const chatDock = createChatDock({
      host,
      resizer: $('chat-resizer'),
      header: $('chat-header'),
      floatHandles: () => host.querySelectorAll('.chat-float-handle'),
      playDust: (enter) => playDust(enter),
      invalidatePillRects: () => invalidatePillRects(),
      phoneModal: () => phoneModal(),
      onAdopt: () => gestures?.notifyClosed(),
    });
    const { setDock, announceLayout, adoptLayout, restoreFromCompact } = chatDock;

    const openBtn = $('chat-btn');
// Closing plays the reverse dust flight: keep .chat-open until it finishes, since
// display:none cannot animate. One clock for every dock.
    const CLOSE_MS = 510;
// Fullscreen shows a clone of the toolbar (ui/fullscreenLayer.js) with the same id, so
// the active state is mirrored onto it via a scoped querySelectorAll.
    const syncFsCloneActive = (on) => {
      for (const el of fsCloneBtns()) el.classList.toggle('active', on);
    };
// No unread dot (the toast already announces a landed turn); a quiet pulse marks work in flight.
    const markChatBusy = (on) => {
      openBtn?.classList.toggle('chat-working', on);
      for (const el of fsCloneBtns()) el.classList.toggle('chat-working', on);
    };
    const fsCloneBtns = () => document.querySelectorAll('#fs-controls-panel #chat-btn');
// The icon to pin the compact popover to: in fullscreen the original measures 0×0.
    const anchorBtn = () => {
      for (const el of fsCloneBtns()) {
        const r = el.getBoundingClientRect();
        if (r.width || r.height) return el;
      }
      return openBtn;
    };
// A float panel flies out of the toolbar icon and back (modalFromIcon/modalToIcon). Fed
// from floatRect: the panel is display:none while closed.
    const setFloatOriginVars = () => {
      if (!host.classList.contains('chat-dock-float')) return;
      const a = anchorBtn()?.getBoundingClientRect?.();
      const onScreen = !!a && a.width > 0 && a.height > 0 && a.bottom > 0 && a.top < window.innerHeight;
      const f = chatDock.rect();
      const cx = onScreen ? a.left + a.width / 2 : f.x + f.w / 2;
      const cy = onScreen ? a.top + a.height / 2 : -Math.max(48, f.h * 0.3);
      host.style.setProperty('--modal-dx', `${Math.round(cx - (f.x + f.w / 2))}px`);
      host.style.setProperty('--modal-dy', `${Math.round(cy - (f.y + f.h / 2))}px`);
      host.style.setProperty('--modal-sx', String(onScreen ? Math.max(a.width / f.w, 0.05) : 0.4));
      host.style.setProperty('--modal-sy', String(onScreen ? Math.max(a.height / f.h, 0.05) : 0.4));
    };
// The panel forms from motes out of where it comes from: a float out of the toolbar
// icon, a docked panel from far past its edge. Measured live.
    const dustPoint = () => {
      if (host.classList.contains('chat-dock-float')) {
        const p = rectCenter(anchorBtn());
        if (p) return p;
      }
      const r = host.getBoundingClientRect();
      return dockAwayPoint(r, chatDock.mode()) || { x: r.left + r.width / 2, y: -Math.max(48, r.height * 0.3) };
    };
    const playDust = (enter) => {
      if (motionReduced()) { settleSurface(host); return; }
      (enter ? surfaceIn : surfaceOut)(host, dustPoint(), { ms: enter ? 630 : CLOSE_MS });
    };
    let closeTimer = null;
// A sequel to run once the close animation finishes (float → compact). Any later
// setOpen supersedes it.
    let afterClose = null;
    const setOpen = (on) => {
      clearTimeout(closeTimer);
      afterClose = null;
      host.classList.remove('chat-closing');
// Any close resets the gesture machine (popover.js notifyClosed), or a leaked 'sticky'
// mode lets a later Alt glide close a reopened panel.
      if (!on) gestures?.notifyClosed();
      if (!on && host.classList.contains('chat-open')) {
        setFloatOriginVars();
        playDust(false);        // …measured while it is still on screen
        host.classList.add('chat-closing');
        openBtn?.classList.remove('active');
        syncFsCloneActive(false);
        closeTimer = setTimeout(() => {
          host.classList.remove('chat-open', 'chat-closing');
          host.removeAttribute('data-drop-owner');
          restoreFromCompact();
          const next = afterClose;
          afterClose = null;
          next?.();
        }, CLOSE_MS);
        return;
      }
      if (on) setFloatOriginVars();
      host.classList.toggle('chat-open', on);
// After the class, or the panel is display:none and there is nothing to measure.
      if (on) playDust(true);
      else settleSurface(host);
      announceLayout();
// Declares "drops over my rect are mine" to controlsBinder's overDropOwner.
      host.toggleAttribute('data-drop-owner', on);
// Same `active` class as fullscreen/incognito, so it inherits their styling.
      openBtn?.classList.toggle('active', on);
      syncFsCloneActive(on);
      if (on) {
        refreshStatus();
        input.focus();
      }
    };
    const showCompact = () => {
      const anchor = anchorBtn()?.getBoundingClientRect?.();
// The popover shape displaces the layout; chatDock restores it when the popover goes.
      if (anchor) chatDock.enterCompact(compactChatRect(anchor, window.innerWidth, window.innerHeight));
      setOpen(true);
    };
    const openCompact = (convert = false) => {
      if (panelIsOpen() && !(convert && !chatDock.isCompact())) { input.focus(); return; }
// Converting a live panel: let the outgoing shape close, then open the compact one;
// mid-close, ride it out via afterClose.
      if (host.classList.contains('chat-closing')) { afterClose = showCompact; return; }
      if (panelIsOpen()) {
        setOpen(false);
        afterClose = showCompact;
        return;
      }
      showCompact();
    };
    if (openBtn) {
      gestures = wireModalOpenGestures(openBtn, {
        openFull: () => setOpen(!host.classList.contains('chat-open')),
        openPopover: () => openCompact(true),
// NOT eagerClick: opening docks the panel, which pushes this icon ~350px along the toolbar, so
// a double-click's second press would miss it. The click waits out DOUBLE_CLICK_MS instead.
        closePopover: () => setOpen(false),
        isPopoverOpen: panelIsOpen,
// Engaged = pointer inside the panel or text typed (focus alone must not count).
        isPeekEngaged: () => host.matches(':hover') || input.value.trim() !== '',
        holdLinger: () => input.value.trim() !== '',
      });
      host.addEventListener('mouseenter', () => gestures.boxEnter());
      host.addEventListener('mouseleave', () => gestures.boxLeave());
    }
// Entering or leaving fullscreen swaps which toolbar is on screen.
    subscribe(EVENTS.fullscreenChanged, () => {
      restoreFromCompact();
      syncFsCloneActive(panelIsOpen());
    });
// The compact shape follows the mini-window contract of every toolbar popover
// (wireModalShell): outside press closes and is swallowed, Escape closes, an Alt glide closes.
    const compactShowing = () => chatDock.isCompact() && panelIsOpen();
// A window opened FROM the panel (assistant settings) stacks over it and owns both
// gestures — closing the panel underneath would dismiss the wrong one first.
    const modalUp = () => [...modalShells].some((s) => s.isOpen());
    document.addEventListener('pointerdown', (e) => {
      if (!compactShowing() || host.contains(e.target) || modalUp()) return;
// The chat icon keeps its own gestures: swallowing its press would turn the toggle into a reopen.
      if (openBtn && openBtn.contains(e.target)) return;
// The row menu floats on the body — pressing it is chat use.
      if (e.target.closest?.('.chat-row-menu')) return;
      e.preventDefault();
      e.stopPropagation();
      setOpen(false);
    }, true);
// Phones show the panel as a centred modal, so it takes backdrop press + Escape; the
// backdrop is display:none on wider viewports.
    const phoneModal = () => typeof matchMedia !== 'undefined' && matchMedia(PHONE_MEDIA).matches;
    $('chat-backdrop')?.addEventListener('pointerdown', (e) => {
      if (!panelIsOpen()) return;
      e.preventDefault();
      setOpen(false);
    });
    document.addEventListener('keydown', (e) => {
      if (e.key !== 'Escape' || modalUp()) return;
      if (compactShowing() || (phoneModal() && panelIsOpen())) setOpen(false);
    });
    $('chat-close').addEventListener('click', () => setOpen(false));

// Clear = a fresh conversation; settings and the working image are untouched.
    const clearBtn = $('chat-clear');
    clearBtn.addEventListener('click', () => {
      if (sending) return;
      clearSharedConversation(app);
      updateControls();
      input.focus();
    });


    chatDock.wireGestures();
    setDock(chatDock.mode());
    updateControls();

// Scripting surface (console/stencilApi.js): the same code paths as the buttons.
// `app.chat` is this panel; §12 persistence wires later under `app.chatPersistence`.
    app.chat = {
      open: () => setOpen(true),
      close: () => setOpen(false),
      isOpen: () => panelIsOpen(),
      dock: (mode) => {
        const m = String(mode || '').toLowerCase();
        if (!DOCKS.includes(m)) throw new Error(`Unknown dock mode "${mode}" — one of ${DOCKS.join(', ')}`);
        adoptLayout();
        setDock(m);
      },
      prompt: async (text, images = []) => {
        if (sending) throw new Error('The assistant is already answering — wait for the current turn');
// The whole batch is checked against the remaining room first, so a rejected call never
// changes the tray (§7 MAX_ATTACHMENTS).
        const room = MAX_ATTACHMENTS - (peekChatController(app)?.attachments.length || 0);
        if (images.length > room) {
          throw new Error(`up to ${MAX_ATTACHMENTS} images per message${room < MAX_ATTACHMENTS ? ` (${room} slot${room === 1 ? '' : 's'} left)` : ''}`);
        }
        for (const u of images) ctrl().addImageDataUrl(u);
        renderAttachments();
        return runTurn(String(text ?? ''));
      },
// The §12.1 display form: settled turns, text only; fresh copies per read.
      history: () => rowsToMessages(chatLog()).map((m) => ({ role: m.role, text: m.text })),
// True when a turn was actually running.
      abort: () => {
        const had = !!turnAbort;
        turnAbort?.abort();
        return had;
      },
// The trash button's shared path (§12: the persisted copy clears too); refused mid-turn.
      clear: () => {
        if (sending) throw new Error('The assistant is answering — stop the turn before clearing');
        clearSharedConversation(app);
        updateControls();
      },
      get isSending() { return sending; },
      controller: ctrl,
// Dictation into this composer — the scripting peer of the "…" item / double-click / hold.
      get voiceInput() { return !!voiceCtl?.isOn(); },
      setVoiceInput: (on) => {
        if (on && !app.voice?.supported) throw new Error('Voice input is not supported in this browser');
        voiceCtl?.setMode(!!on);
      },
    };
  }
}
define('stencil-chat-panel', StencilChatPanel);
