import { StencilElement, hostTag, define } from './base.js';
import { popoverPosition, wireModalOpenGestures } from './popover.js';
import { notify, PHONE_MEDIA } from '../utils.js';
import { icon } from './icons.js';
import { probeProvider, PROVIDER_LABELS } from '../llm/llmClient.js';
import { loadLlmSettings, serverBearerToken } from '../llm/llmSettings.js';
import {
  sharedChatController, peekChatController, runLoggedChatTurn, closedTurnToast, queueAttachments,
  cacheProbe, probeStatusClass, chatLog, onChatLog, clearSharedConversation, requeueRowAttachments,
  chatTurnInFlight,
} from '../llm/chatSession.js';
import { rowsToMessages } from '../llm/chatStore.js';
import { MAX_ATTACHMENTS } from '../llm/chatController.js';
import { mediaFilesFromData, extractDraggedImageUrl, fetchDraggedMediaFile } from '../core/dragImageUrl.js';
import { surfaceIn, surfaceOut, settleSurface, dockAwayPoint, motionReduced } from './motion.js';
import {
  renderChatLog, stickToBottom, chatAttachmentChips, wireInputSizer, trackPointer, wireChatSuggestions,
  chatSuggestionsHtml, chatDropCueHtml, chatComposerActionsHtml, syncComposerControls, wireChatComposer, wireChatMoreMenu,
  notifyAttachmentsChanged, CHAT_ATTACHMENTS_EVENT, wireChatRowMenu, rowMenuHitsJumps,
  chatPopupOpen, CHAT_POPUP_EVENT,
} from './chatView.js';

// ── Component: AI assistant chat panel ──────────────────────────
// Dockable chat with the configured LLM (llm-contract.md). Plans execute against the
// frozen window.stencil facade — the panel never edits pixels itself. Layout (host
// classes chat-dock-*, --chat-size, a clamped float rect) is session-only; the header
// strip is the drag handle — dragging a docked panel undocks it, edge zones re-dock.
const DOCKS = ['left', 'right', 'top', 'bottom', 'float'];

const FLOAT_DEFAULT = { x: 80, y: 80, w: 360, h: 440 };
const DRAG_THRESHOLD_PX = 4;      // plain header clicks must not twitch the panel
const DOCK_MIN_SIZE = 240;
const DOCK_MAX_FRACTION = 0.8;    // docked panel never exceeds 80% of the viewport

// Clamp a float rect so the WHOLE panel (right/bottom edges included) stays inside
// the vw×vh viewport. Pure — unit-tested; the panel wires it to window.inner*.
export const FLOAT_MIN_W = 280;
export const FLOAT_MIN_H = 220;
export const clampFloatRect = (r, vw, vh) => {
  const w = Math.max(FLOAT_MIN_W, Math.min(Math.round(r?.w || FLOAT_DEFAULT.w), vw));
  const h = Math.max(FLOAT_MIN_H, Math.min(Math.round(r?.h || FLOAT_DEFAULT.h), vh));
  return {
    x: Math.max(0, Math.min(Math.round(r?.x || 0), vw - w)),
    y: Math.max(0, Math.min(Math.round(r?.y || 0), vh - h)),
    w,
    h,
  };
};

// The compact shape the toolbar icon's popover gestures open (ui/popover.js): the
// panel floated SMALL and pinned next to the icon, sized like the context-menu chat
// flyout. Pure — rects in, a clamped float rect out — so it's unit-testable.
export const COMPACT_CHAT_W = 340;
export const COMPACT_CHAT_H = 460;
export const compactChatRect = (anchor, vw, vh) => {
  const w = Math.min(COMPACT_CHAT_W, vw);
  const h = Math.min(COMPACT_CHAT_H, vh);
  const p = popoverPosition({ anchor, box: { width: w, height: h }, viewport: { width: vw, height: vh } });
  return clampFloatRect({ x: p.left, y: p.top, w, h }, vw, vh);
};

// Resize a float rect by dragging edge/corner `dir` (n|s|e|w|ne|nw|se|sw) by dx/dy.
// The opposite edge stays anchored; the moving edge is clamped to the min size and
// the vw×vh viewport. Pure — unit-tested.
export const resizeFloatRect = (r, dir, dx, dy, vw, vh) => {
  let { x, y, w, h } = r;
  const right = x + w, bottom = y + h;
  if (dir.includes('e')) w = Math.min(Math.max(FLOAT_MIN_W, w + dx), vw - x);
  if (dir.includes('s')) h = Math.min(Math.max(FLOAT_MIN_H, h + dy), vh - y);
  if (dir.includes('w')) { w = Math.min(Math.max(FLOAT_MIN_W, w - dx), right); x = right - w; }
  if (dir.includes('n')) { h = Math.min(Math.max(FLOAT_MIN_H, h - dy), bottom); y = bottom - h; }
  return { x: Math.round(x), y: Math.round(y), w: Math.round(w), h: Math.round(h) };
};

// Which edge drop zone (if any) a viewport point falls in during a header drag.
// Corners resolve to the NEAREST edge; null = keep floating. Pure — unit-tested.
export const DOCK_ZONE_BAND = 72;
export const dockZoneAt = (x, y, vw, vh, band = DOCK_ZONE_BAND) => {
  const dist = { left: x, right: vw - x, top: y, bottom: vh - y };
  let best = null;
  for (const side of ['left', 'right', 'top', 'bottom']) {
    if (dist[side] <= band && (best == null || dist[side] < dist[best])) best = side;
  }
  return best;
};

// The configure-gear's status tooltip is a TABLE (.chat-status-tip): one row per
// fact, the Status cell coloured by `state` (ok|error|connecting). Pure — unit-tested.
export const gearStatusRows = (probe) => {
  if (!probe) return [{ label: 'Status', value: 'Checking the configured LLM…', state: 'connecting' }];
  if (probe.provider === 'none') {
    return [
      { label: 'Provider', value: PROVIDER_LABELS.none },
      { label: 'Status', value: 'Assistant turned off — nothing is sent anywhere', state: 'error' },
    ];
  }
  const rows = [{ label: 'Provider', value: PROVIDER_LABELS[probe.provider] || probe.provider || '—' }];
  if (probe.url) rows.push({ label: 'Endpoint', value: probe.url.replace(/^https?:\/\//i, '') });
  rows.push({ label: 'Model', value: probe.model || 'server default' });
  rows.push(probe.ok
    ? { label: 'Status', value: `Connected${probe.detail ? ` — ${probe.detail}` : ''}`, state: 'ok' }
    : { label: 'Status', value: probe.detail || 'Unreachable', state: 'error' });
  return rows;
};

// The tooltip's footer lines (pre-line text under the table): what to try / the
// call to action, always ending with the click hint. Pure — unit-tested.
export const gearTipFootText = (probe) => {
  const lines = [];
  // Connected needs no prose — the table above already says provider/model/status.
  if (!probe || probe.provider === 'none' || probe.ok) { /* the table says it all */ }
  else {
    lines.push(`No LLM reachable${probe.url ? ` at ${probe.url}` : ''} — ${probe.provider === 'ollama' ? 'start Ollama or ' : ''}configure another provider.`);
  }
  lines.push('Click to configure the assistant');
  return lines.join('\n');
};

export class StencilChatPanel extends StencilElement {
  static inner() {
    return `
        <div class="chat-header" id="chat-header">
            <span class="chat-title" id="chat-title">${icon('sparkle', { size: 14 })}<span class="chat-title-text">Assistant</span></span>
            <span class="chat-dock-btns">
                <button id="chat-dock-left-btn" class="chat-hbtn chat-dock-btn" data-dock="left" title="Dock left — or drag the header to an edge">${icon('chevron-left', { size: 13 })}</button>
                <button id="chat-dock-top-btn" class="chat-hbtn chat-dock-btn" data-dock="top" title="Dock top — or drag the header to an edge">${icon('chevron-up', { size: 13 })}</button>
                <button id="chat-dock-bottom-btn" class="chat-hbtn chat-dock-btn" data-dock="bottom" title="Dock bottom — or drag the header to an edge">${icon('chevron-down', { size: 13 })}</button>
                <button id="chat-dock-right-btn" class="chat-hbtn chat-dock-btn" data-dock="right" title="Dock right — or drag the header to an edge">${icon('chevron-right', { size: 13 })}</button>
                <button id="chat-float-btn" class="chat-hbtn chat-dock-btn" data-dock="float" title="Float — drag the header to move">${icon('maximize', { size: 13 })}</button>
            </span>
            <button id="chat-close" class="chat-hbtn" title="Close assistant">${icon('x', { size: 13 })}</button>
        </div>
        <div class="chat-transcript" id="chat-transcript">
            <div class="chat-empty" id="chat-empty">
                ${chatSuggestionsHtml()}
            </div>
        </div>
        <div class="chat-jumps" id="chat-jumps">
            <button id="chat-jump-top" class="chat-jump-btn" title="Jump to the beginning">${icon('chevron-up', { size: 14 })}</button>
            <button id="chat-jump-bottom" class="chat-jump-btn" title="Jump to the latest message">${icon('chevron-down', { size: 14 })}</button>
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
  // Sibling (not ancestor) backdrop: a dock/float must leave the page clickable.
  // CSS shows it only under the ≤680px modal shape.
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

    const tokenFor = (url) => serverBearerToken(app, url);

    // ── The app's ONE controller (js/llm/chatSession.js), created lazily so it
    // consumes the FROZEN window.stencil (createStencil runs after stencil:ready
    // wires this panel). The context-menu chat shares it — same conversation. ──
    const ctrl = () => sharedChatController(app);

    // ── Transcript: a view of the SHARED row log (js/llm/chatSession.js), so the
    // context-menu flyout and this panel always show the same messages in the same
    // order — including the history a surface missed while it was closed. ──
    const paint = () => renderChatLog(transcript, chatLog(), {
      onConfigure: () => gearBtn.click(),
      // An expired collaboration-server session is fixed in Connections, not in the
      // provider settings — the card's CTA goes straight there.
      onReconnect: () => document.getElementById('connect-btn')?.click(),
      // §11: answering a choice card sends the answer as the user's next turn — the same
      // path as typing it, so history, results and error handling are identical.
      onAskSubmit: (answer) => { runTurn(answer).catch(() => { /* rendered in the transcript */ }); },
      // Retry: the same text through the normal send path, attachments re-queued.
      // Guarded on the SHARED in-flight flag too — a turn started from the other
      // surface is just as much a turn, and re-sending over it logged the prompt twice.
      onRetry: (text) => {
        if (sending || chatTurnInFlight()) return;
        peekChatController(app)?.requeueLastTurnAttachments?.();
        runTurn(text).catch(() => { /* rendered in the transcript */ });
      },
    });
    onChatLog(paint);
    paint();   // renders whatever the conversation already holds

    // ── Jump pills over the transcript's bottom edge: ⌄ appears once the view has
    // scrolled up from the latest message, ⌃ once it left the very beginning — both
    // mid-log, neither while the log fits (desktop chatDock parity). ──
    const jumps = $('chat-jumps');
    // …and they yield to the hovered row's "…" when the two would overlap (the pills
    // win the paint order). A displayed pill's rect is cached — a hidden one has none.
    let hoverRow = null;
    let pillRects = [];
    const syncJumps = () => {
      const max = transcript.scrollHeight - transcript.clientHeight;
      const shown = [$('chat-jump-top'), $('chat-jump-bottom')]
        .map((b) => b?.getBoundingClientRect?.()).filter((r) => r && r.width > 0);
      if (shown.length) pillRects = shown;
      const btn = hoverRow?.querySelector?.('.chat-row-menu-btn')?.getBoundingClientRect?.();
      // Two reasons to stand down, and they compose: the hovered row's "…" would touch
      // a pill, OR ANY chat popup is open — both open into this very corner.
      const standDown = chatPopupOpen() || rowMenuHitsJumps(btn, pillRects);
      jumps.classList.toggle('can-up', transcript.scrollTop > 12 && !standDown);
      jumps.classList.toggle('can-down', max - transcript.scrollTop > 12 && !standDown);
    };
    // The pills float OVER the transcript, so a cursor on one leaves no row hovered —
    // hovering a pill can never hide it.
    transcript.addEventListener('mouseover', (e) => {
      const row = e.target?.closest?.('.chat-msg');
      if (row === hoverRow) return;
      hoverRow = row && transcript.contains(row) ? row : null;
      syncJumps();
    });
    transcript.addEventListener('mouseleave', () => { hoverRow = null; syncJumps(); });
    // Both edges of any chat popup (chatView announces open AND close), from EITHER
    // surface: the flyout shares the one row menu and has a composer "…" of its own.
    window.addEventListener(CHAT_POPUP_EVENT, syncJumps);
    transcript.addEventListener('scroll', syncJumps, { passive: true });
    new MutationObserver(syncJumps).observe(transcript, { childList: true, subtree: true });
    $('chat-jump-top').addEventListener('click', () => transcript.scrollTo({ top: 0, behavior: 'smooth' }));
    $('chat-jump-bottom').addEventListener('click', () => transcript.scrollTo({ top: transcript.scrollHeight, behavior: 'smooth' }));

    // ── Provider status: a cheap probe on open / after settings change, surfaced as
    // the coloured dot ON the configure gear plus the gear's rich tooltip (rendered
    // by the app's shared instant tooltip via `title`). Never blocks sending. ──
    const statusDot = $('chat-status-dot');
    const gearBtn = $('chat-settings-btn');
    // The rich provider-status tooltip now hangs off the … TRIGGER: the gear
    // itself lives inside the menu, so it is hidden most of the time (the dot
    // moved with it for the same reason).
    const statusHost = $('chat-more-btn') || gearBtn;

    // ── Gear status tooltip: a small themed TABLE (provider/endpoint/model/status,
    // the status cell coloured), fixed-positioned above the gear like #app-tooltip.
    // Content re-renders live if the probe lands while the tip is showing. ──
    let lastProbe = null;   // null = probe in flight
    const gearTip = document.createElement('div');
    gearTip.className = 'chat-status-tip';
    document.body.appendChild(gearTip);
    const renderGearTip = () => {
      gearTip.textContent = '';
      const table = document.createElement('table');
      for (const r of gearStatusRows(lastProbe)) {
        const tr = document.createElement('tr');
        const th = document.createElement('th');
        th.textContent = r.label;
        const td = document.createElement('td');
        td.textContent = r.value;
        if (r.state) td.classList.add(`chat-status-${r.state}`);
        tr.append(th, td);
        table.appendChild(tr);
      }
      const foot = document.createElement('div');
      foot.className = 'chat-status-tip-foot';
      foot.textContent = gearTipFootText(lastProbe);
      gearTip.append(table, foot);
    };
    const placeGearTip = () => {
      const r = statusHost.getBoundingClientRect();
      const t = gearTip.getBoundingClientRect();
      const pad = 8;
      const x = Math.max(pad, Math.min(r.left + r.width / 2 - t.width / 2, window.innerWidth - t.width - pad));
      const above = r.top - t.height - pad;
      gearTip.style.left = `${Math.round(x)}px`;
      gearTip.style.top = `${Math.round(above >= pad ? above : r.bottom + pad)}px`;
    };
    const showGearTip = () => { renderGearTip(); gearTip.classList.add('visible'); placeGearTip(); };
    const hideGearTip = () => gearTip.classList.remove('visible');
    statusHost.addEventListener('pointerenter', showGearTip);
    statusHost.addEventListener('focus', showGearTip);
    statusHost.addEventListener('pointerleave', hideGearTip);
    statusHost.addEventListener('blur', hideGearTip);
    statusHost.addEventListener('pointerdown', hideGearTip);

    const setDotState = (state, probe) => {
      statusDot.className = `conn-status conn-status-${state}`;
      lastProbe = probe;
      // Publish it: the context-menu gear shows the same dot without its own probe.
      if (probe) cacheProbe(loadLlmSettings(), probe);
      if (gearTip.classList.contains('visible')) showGearTip();
    };
    // One probe at a time, but a refresh requested MID-probe must not be lost — it
    // queues and re-runs once, so the dot/tooltip reflect the LATEST settings, not
    // the probe that happened to land last.
    let probing = false;
    let reprobe = false;
    const refreshStatus = async () => {
      if (probing) { reprobe = true; return; }
      probing = true;
      setDotState('connecting', null);
      try {
        const probe = await probeProvider(loadLlmSettings(), { getToken: tokenFor });
        setDotState(probeStatusClass(probe), probe);
      } finally {
        probing = false;
        if (reprobe) { reprobe = false; refreshStatus(); }
      }
    };
    // Re-probe when the settings modal persists a change (it fires this event).
    window.addEventListener('stencil:llm-settings-changed', () => refreshStatus());

    // ── Attachments row (shared renderer — the context-menu composer paints the
    // very same queue, so both repaint on the shared change event) ──
    const renderAttachments = () => {
      // peek, never create: the controller must not exist before window.stencil is frozen.
      chatAttachmentChips(attachList, peekChatController(app));
    };
    window.addEventListener(CHAT_ATTACHMENTS_EVENT, renderAttachments);

    renderAttachments();

    // Queue Files (from the picker, clipboard paste, or a drop on the panel).
    const attachFiles = async (files) => {
      await queueAttachments(ctrl(), files, (err) => notify(`Attachment failed — ${err.message}`, 'fail'));
      renderAttachments();
      notifyAttachmentsChanged();
    };
    const attachBtn = $('chat-attach-btn');

    // ── Disabled states (shared sync): while a turn is in flight the send button
    // BECOMES the Stop button and attach pauses; the panel adds its Clear button.
    // Only one turn at a time — send() and the facade both guard on `sending`.
    let sending = false;
    let turnAbort = null;
    const updateControls = () => {
      const queued = peekChatController(app)?.attachments.length ?? 0;
      syncComposerControls({ sendBtn, attachBtn, input }, sending, { attachFull: queued >= MAX_ATTACHMENTS });
      // Clear is pointless mid-turn AND on an empty conversation.
      clearBtn.disabled = sending || !!transcript.querySelector('.chat-empty');
    };

    // Phone modal (components.css ≤680px) hides the drag sizer — the textarea
    // auto-grows with its content there instead (clamped by its CSS max-height).
    const autoGrow = () => {
      if (typeof matchMedia === 'undefined' || !matchMedia(PHONE_MEDIA).matches) return;
      input.style.height = 'auto';
      input.style.height = `${input.scrollHeight + 2}px`;
    };

    // Empty-state suggestion chips: click prefills the input (editable before sending).
    // Delegated on the transcript, so the chips still work after the block is rebuilt.
    wireChatSuggestions(transcript, (prompt) => {
      input.value = prompt;
      updateControls();
      input.focus();
    });

    // A turn that lands while the panel is CLOSED must not vanish: surface a short
    // status as a clickable toast (bottom-left balloon) that reopens the chat.
    // Open = visible AND not mid-close (the closing slide keeps .chat-open).
    const panelIsOpen = () =>
      host.classList.contains('chat-open') && !host.classList.contains('chat-closing');
    const closedToast = (res) => {
      const toast = closedTurnToast(res);   // one shape for every surface
      if (panelIsOpen() || !toast) return;
      notify(toast.text, toast.type, { onClick: () => setOpen(true) });
      markChatUnread(true);   // …and the toolbar button carries the dot until it is read
    };

    // ── Send loop: the shared logged-turn frame (rows in the SHARED log — this panel
    // and the context-menu flyout both render them). runTurn rethrows a failed turn
    // so the scripting path (stencil.prompt) gets the typed rejection too. ──
    const runTurn = async (text) => {
      sending = true;
      updateControls();
      markChatBusy(!panelIsOpen());
      const res = await runLoggedChatTurn(ctrl(), text, {
        settings: loadLlmSettings(),
        // Sending is an explicit "take me to the newest" — pin to the bottom even
        // if the user had scrolled up (the render's stickiness only follows when
        // already at the bottom).
        begin: (abort) => { turnAbort = abort; stickToBottom(transcript); },
        onResult: (r) => {
          if (!r.ok && r.kind === 'unreachable') refreshStatus();   // the dot + tooltip reflect it
          // Framing, truncation and "an abort says nothing" all live in the shared
          // builder, so this panel and the flyout can never drift apart again.
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
      if (!res.ok) throw res.error;   // the scripting path gets the typed rejection
      return res.entry;
    };
    wireChatMoreMenu('chat', document, { onOpen: updateControls });   // attach / clear / settings live behind the …
    wireChatComposer({ input, sendBtn, attachBtn, attachInput: $('chat-attach-input') }, {
      isSending: () => sending,
      abort: () => turnAbort?.abort(),
      // autoGrow: phone modal — shrink the just-emptied textarea back down.
      submit: (text) => {
        updateControls();
        autoGrow();
        runTurn(text).catch(() => { /* rendered in the transcript */ });
      },
      attachFiles,
      onInput: () => { updateControls(); autoGrow(); },
    });
    // Right-click on a transcript row: the SHARED row menu (chatView.js). Insert
    // appends into THIS composer; Resend re-queues the row's original attachments
    // and re-sends through the same runTurn path a composer send takes.
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

    // ── Resizable input: a slider-style strip ABOVE the input row (the native
    // corner grip is gone — the textarea is bottom-anchored, so only the top edge
    // can move). Clamped in CSS; session-only, like the rest of the layout. ──
    const inputSizer = $('chat-input-sizer');
    wireInputSizer(inputSizer, input, { host });

    // ── Clipboard paste + drop-on-panel attach: image/video FILES land as chat
    // attachments (mediaFilesFromData — the same extraction the global paste/drop
    // wiring uses); text pastes stay native, non-file drops fall through. ──
    host.addEventListener('paste', async (e) => {
      if (!host.classList.contains('chat-open')) return;
      const files = mediaFilesFromData(e.clipboardData);   // read SYNC, before any await
      if (!files.length) return;   // plain text → native paste into the textarea
      e.preventDefault();
      e.stopPropagation();         // never falls through to the global canvas paste
      // No toast: the chip appearing in the composer IS the confirmation.
      await attachFiles(files);
    });
    // ── Drop-to-attach: the COMPOSER acts on a drop, but the whole panel SWALLOWS
    // one — letting it fall through popped the canvas's "Open dropped image" dialog,
    // never what dragging onto a chat means. A miss says where to aim. ──
    host.addEventListener('dragover', (e) => {
      if (!host.classList.contains('chat-open')) return;
      e.preventDefault();   // …and it must still BUBBLE: the document's drop-owner
                            // branch is what hides the global overlay over the panel.
      try { e.dataTransfer.dropEffect = 'copy'; } catch { /* older DnD */ }
    });
    host.addEventListener('drop', (e) => {
      if (!host.classList.contains('chat-open')) return;
      // The composer's own handler runs first (it is deeper) and stops propagation;
      // reaching here means the drop landed on the transcript or the header.
      e.preventDefault();
      e.stopPropagation();
      if (mediaFilesFromData(e.dataTransfer).length
          || extractDraggedImageUrl((t) => e.dataTransfer.getData(t))) {
        notify('Drop it on the message box to attach it', 'info');
      }
    });
    // The TEXT BOX is the drop target, not the whole row: dragging over the send/…
    // buttons neither lights the cue nor attaches — that drop bubbles to the panel's
    // swallow above, which says where to aim (desktop showDropCue parity).
    const dropRow = $('chat-input-wrap');
    dropRow.addEventListener('dragover', (e) => {
      if (!host.classList.contains('chat-open')) return;
      e.preventDefault();
      // MUST bubble to the document dragover: its drop-owner branch is what hides
      // the global drop overlay while the drag is over this row (the [data-drop-owner]
      // attribute set below is how it knows this row handles its own drops).
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
      e.stopPropagation();         // this drop belongs to the chat, not the canvas
      const files = mediaFilesFromData(e.dataTransfer);
      if (files.length) { await attachFiles(files); return; }
      // An image dragged from another PAGE (or from the extension's list) carries no
      // File at all — just a URL in uri-list/html. Fetching it here is what makes
      // "drag a picture into the chat" work.
      const url = extractDraggedImageUrl((t) => e.dataTransfer.getData(t));
      if (!url) { notify('Nothing to attach from that drop', 'fail'); return; }
      try {
        await attachFiles([await fetchDraggedMediaFile(url, { accept: /^(image|video)\// })]);
      } catch (err) {
        notify(`Couldn't attach that image — ${err.message}`, 'fail');
      }
    });

    // ── Docking / float placement. Layout is SESSION-ONLY by design: every page
    // load starts closed, docked left, at the default sizes. ──
    let dock = 'left';
    const clampRect = (r) => clampFloatRect(r, window.innerWidth, window.innerHeight);
    let floatRect = clampRect(FLOAT_DEFAULT);

    const applyFloatRect = () => {
      host.style.left = floatRect.x + 'px';
      host.style.top = floatRect.y + 'px';
      host.style.width = floatRect.w + 'px';
      host.style.height = floatRect.h + 'px';
      host.style.right = 'auto';
      host.style.bottom = 'auto';
    };
    const dockBtns = [...host.querySelectorAll('.chat-dock-btn')];
    // Docking top/bottom takes real height from the editor column, so the viewport and
    // coordinates panel re-measure against what is left. Announced as an event, not a
    // ResizeObserver: the listeners resize the very elements such an observer would
    // watch. Toasts dodge a docked chat: the notify stack reads these vars.
    const updateNotifyInset = () => {
      const open = host.classList.contains('chat-open');
      const r = open ? host.getBoundingClientRect() : { width: 0, height: 0 };
      const left = open && host.classList.contains('chat-dock-left') ? Math.round(r.width) : 0;
      const bottom = open && host.classList.contains('chat-dock-bottom') ? Math.round(r.height) : 0;
      document.body.style.setProperty('--chat-inset-left', `${left}px`);
      document.body.style.setProperty('--chat-inset-bottom', `${bottom}px`);
    };
    if (typeof ResizeObserver !== 'undefined') new ResizeObserver(updateNotifyInset).observe(host);
    const announceLayout = () => {
      updateNotifyInset();
      try { window.dispatchEvent(new Event('stencil:chat-layout-changed')); } catch { /* no DOM */ }
    };
    const setDock = (mode) => {
      dock = mode;
      for (const d of DOCKS) host.classList.toggle(`chat-dock-${d}`, d === mode);
      host.removeAttribute('style');   // clear any float inline rect
      if (mode === 'float') applyFloatRect();
      for (const b of dockBtns) b.classList.toggle('chat-dock-btn-active', b.dataset.dock === mode);
      // The dock swap used to restart the matching slide; it re-forms out of the NEW
      // edge instead. Only while on screen — a closed panel has nothing to measure.
      if (host.classList.contains('chat-open') && !host.classList.contains('chat-closing')) playDust(true);
      announceLayout();
    };
    // A compact float opened by the popover gestures is TRANSIENT: once it closes, the
    // prior layout comes back — only a layout the user chose (dock button, header
    // drag, edge resize) sticks for the session.
    let compactPopover = false;
    let dockBeforeCompact = dock;
    let floatRectBeforeCompact = null;
    let gestures = null;   // the icon's gesture machine, assigned below
    // Adopting a layout also resets the gesture machine: its popover is gone,
    // so an Alt glide over other icons must not closeFromGlide THIS panel.
    const adoptLayout = () => { compactPopover = false; gestures?.notifyClosed(); };
    const restoreFromCompact = () => {
      if (!compactPopover) return;
      compactPopover = false;
      if (floatRectBeforeCompact) floatRect = clampRect(floatRectBeforeCompact);
      setDock(dockBeforeCompact);
    };
    for (const b of dockBtns) b.addEventListener('click', () => { adoptLayout(); setDock(b.dataset.dock); });

    // ── Open / close, toggled by the toolbar button ──
    const openBtn = $('chat-btn');
    // Closing plays the reverse slide (.chat-closing in animations.css): keep
    // .chat-open until it finishes, since display:none can't animate. A float
    // plays the longer flight back into the icon (modalToIcon).
    const CLOSE_ANIM_MS = 260;
    const closeMs = () => (host.classList.contains('chat-dock-float') ? 340 : CLOSE_ANIM_MS);
    // Fullscreen shows a CLONE of the toolbar (ui/fullscreenLayer.js), so toggling
    // .active on the display:none original leaves the visible copy stuck — mirror onto
    // the clone. Scoped querySelectorAll, not getElementById: the clone carries the
    // same id, and getElementById only ever returns the original.
    const syncFsCloneActive = (on) => {
      for (const el of fsCloneBtns()) el.classList.toggle('active', on);
    };
    // Unread: a turn that landed while the chat was not visible leaves a dot ON the
    // toolbar icon — the toast is transient. Same clone mirroring as `.active`, and
    // purely a class, so the button's box never moves.
    const markChatUnread = (on) => {
      openBtn?.classList.toggle('chat-unread', on);
      for (const el of fsCloneBtns()) el.classList.toggle('chat-unread', on);
    };
    // …and the same dot, quietly pulsing, while a turn is in flight behind a closed
    // chat: "still working" and "there is an answer" read as one affordance.
    const markChatBusy = (on) => {
      openBtn?.classList.toggle('chat-working', on);
      for (const el of fsCloneBtns()) el.classList.toggle('chat-working', on);
    };
    const fsCloneBtns = () => document.querySelectorAll('#fs-controls-panel #chat-btn');
    // The icon to PIN the compact popover to. In fullscreen the original is
    // display:none and measures 0×0 at the origin, which would pin the panel to
    // the top-left corner instead of the icon the user actually clicked.
    const anchorBtn = () => {
      for (const el of fsCloneBtns()) {
        const r = el.getBoundingClientRect();
        if (r.width || r.height) return el;
      }
      return openBtn;
    };
    // A FLOAT panel flies out of the toolbar icon and shrinks back into it (the
    // modalFromIcon/modalToIcon motion every modal uses). Fed from floatRect, not a
    // measured rect: the panel is display:none while closed — nothing to measure.
    const setFloatOriginVars = () => {
      if (!host.classList.contains('chat-dock-float')) return;
      const a = anchorBtn()?.getBoundingClientRect?.();
      const onScreen = !!a && a.width > 0 && a.height > 0 && a.bottom > 0 && a.top < window.innerHeight;
      const cx = onScreen ? a.left + a.width / 2 : floatRect.x + floatRect.w / 2;
      const cy = onScreen ? a.top + a.height / 2 : -Math.max(48, floatRect.h * 0.3);
      host.style.setProperty('--modal-dx', `${Math.round(cx - (floatRect.x + floatRect.w / 2))}px`);
      host.style.setProperty('--modal-dy', `${Math.round(cy - (floatRect.y + floatRect.h / 2))}px`);
      host.style.setProperty('--modal-sx', String(onScreen ? Math.max(a.width / floatRect.w, 0.05) : 0.4));
      host.style.setProperty('--modal-sy', String(onScreen ? Math.max(a.height / floatRect.h, 0.05) : 0.4));
    };
    // ── Dust (js/ui/motion.js): the panel forms from motes and comes apart into them,
    // out of exactly where it used to come from — a FLOAT out of the toolbar icon, a
    // docked panel from far past the edge it is docked to, so the stream still runs
    // along the slide's own direction. Measured live; nothing is guessed.
    const dustPoint = () => {
      if (host.classList.contains('chat-dock-float')) {
        const a = anchorBtn()?.getBoundingClientRect?.();
        if (a && (a.width || a.height)) return { x: a.left + a.width / 2, y: a.top + a.height / 2 };
      }
      const r = host.getBoundingClientRect();
      // No icon and no dock edge to lean on: from above, the modal shell's own fallback.
      return dockAwayPoint(r, dock) || { x: r.left + r.width / 2, y: -Math.max(48, r.height * 0.3) };
    };
    const playDust = (enter) => {
      if (motionReduced()) { settleSurface(host); return; }
      (enter ? surfaceIn : surfaceOut)(host, dustPoint(), { ms: enter ? 420 : closeMs() });
    };
    let closeTimer = null;
    // A sequel queued to run once the CLOSE animation has finished (the float → compact
    // shape swap). Any later setOpen supersedes it, so a close that gets interrupted
    // never re-opens the panel behind the user's back.
    let afterClose = null;
    const setOpen = (on) => {
      clearTimeout(closeTimer);
      afterClose = null;
      host.classList.remove('chat-closing');
      // Any close resets the gesture machine (popover.js notifyClosed): a
      // leaked 'sticky' mode would let a later Alt glide close a panel the
      // user reopened docked.
      if (!on) gestures?.notifyClosed();
      if (!on && host.classList.contains('chat-open')) {
        setFloatOriginVars();   // shrink back into the icon it came from
        playDust(false);        // …measured while it is still on screen
        host.classList.add('chat-closing');
        openBtn?.classList.remove('active');
        syncFsCloneActive(false);
        closeTimer = setTimeout(() => {
          host.classList.remove('chat-open', 'chat-closing');
          host.removeAttribute('data-drop-owner');
          restoreFromCompact();   // popover shape dies with the popover
          const next = afterClose;
          afterClose = null;
          next?.();   // the shape swap re-opens from here, never on top of the close
        }, closeMs());
        return;
      }
      if (on) setFloatOriginVars();
      host.classList.toggle('chat-open', on);
      // After the class, or the panel is display:none and there is nothing to measure.
      if (on) playDust(true);
      else settleSurface(host);
      announceLayout();
      // Declares "drops over my rect are mine" to the global drag/drop wiring
      // (controlsBinder's overDropOwner), in lockstep with .chat-open — the canvas
      // must not light its drop zones under an open chat.
      host.toggleAttribute('data-drop-owner', on);
      // The toolbar toggle uses the SAME `active` class as fullscreen/incognito, so
      // it inherits their accent-fill + white-glyph styling in both themes.
      openBtn?.classList.toggle('active', on);
      syncFsCloneActive(on);
      if (on) {
        markChatUnread(false);   // opening it IS reading it
        refreshStatus();   // async, never blocks the panel or sending
        input.focus();
      }
    };
    // The toolbar icon answers the app-wide popover gestures (ui/popover.js): click
    // toggles the panel; dblclick / right-click / long press open the COMPACT float
    // pinned to the icon — same conversation, same DOM. `convert` = the deliberate
    // compact gesture: the panel may already be open and must still re-shape.
    const showCompact = () => {
      const anchor = anchorBtn()?.getBoundingClientRect?.();
      if (anchor) {
        if (!compactPopover) {   // remember the layout this popover displaces
          dockBeforeCompact = dock;
          floatRectBeforeCompact = { ...floatRect };
        }
        compactPopover = true;
        floatRect = compactChatRect(anchor, window.innerWidth, window.innerHeight);
        setDock('float');
      }
      setOpen(true);
    };
    const openCompact = (convert = false) => {
      if (panelIsOpen() && !(convert && !compactPopover)) { input.focus(); return; }
      // Converting a panel ALREADY on screen: let the outgoing shape play its ordinary
      // close, then open the compact one — re-pointing geometry under a live panel
      // blinks the float out with no exit. Mid-close (the double-click's eager first
      // click started the exit): ride it out via afterClose instead of cancelling.
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
        // The toolbar toggle opens NOW: its popover gesture only re-shapes this same
        // panel, so there is no open-and-shut flash to protect against, and waiting a
        // double-click interval to find that out made every plain click feel laggy.
        eagerClick: true,
        // HOLD-to-peek: an Alt+hover-opened compact chat closes on Alt release; the
        // isPopoverOpen guard keeps the peek from adopting a panel the user had open,
        // and the glide only ever closes what the machine itself opened.
        closePopover: () => setOpen(false),
        isPopoverOpen: panelIsOpen,
        // Engaged = pointer inside the panel, or something typed into the
        // composer (openCompact auto-focuses the input, so focus alone must not
        // count) — releasing Alt then keeps the chat open, hover-bound.
        isPeekEngaged: () => host.matches(':hover') || input.value.trim() !== '',
        // A lingering chat never hover-closes while the composer holds text.
        holdLinger: () => input.value.trim() !== '',
      });
      // Crossing the panel edge drives the linger close.
      host.addEventListener('mouseenter', () => gestures.boxEnter());
      host.addEventListener('mouseleave', () => gestures.boxLeave());
    }
    // Entering or leaving fullscreen swaps which toolbar is on screen, stranding a
    // compact popover on an icon that no longer exists — drop the popover shape. The
    // freshly cloned toolbar also needs the open state stamped onto it (snapshot).
    window.addEventListener('stencil:fullscreen-changed', () => {
      if (compactPopover) restoreFromCompact();
      syncFsCloneActive(panelIsOpen());
    });
    // The COMPACT shape follows the mini-window contract of every toolbar popover
    // (base.js wireModalShell): outside press closes and is swallowed, Escape closes,
    // an Alt glide closes via the shared registry. The panel's normal shapes —
    // docked, or a float the user adopted — stay persistent.
    const compactShowing = () => compactPopover && panelIsOpen();
    document.addEventListener('pointerdown', (e) => {
      if (!compactShowing() || host.contains(e.target)) return;
      // The chat icon keeps its own gestures (click toggles, dblclick reopens
      // compact) — swallowing its press would turn the toggle into a reopen.
      if (openBtn && openBtn.contains(e.target)) return;
      // The transcript's row menu floats on the BODY — pressing it is chat use,
      // not "outside the compact panel".
      if (e.target.closest?.('.chat-row-menu')) return;
      e.preventDefault();
      e.stopPropagation();
      setOpen(false);
    }, true);
    // Phones show the panel as a centred modal, so it takes the modal dismissals a
    // dock/float deliberately lacks: backdrop press + Escape. The backdrop is
    // display:none on wider viewports, so it can never fire for a dock/float.
    const phoneModal = () => typeof matchMedia !== 'undefined' && matchMedia(PHONE_MEDIA).matches;
    $('chat-backdrop')?.addEventListener('pointerdown', (e) => {
      if (!panelIsOpen()) return;
      e.preventDefault();
      setOpen(false);
    });
    document.addEventListener('keydown', (e) => {
      if (e.key !== 'Escape') return;
      if (compactShowing() || (phoneModal() && panelIsOpen())) setOpen(false);
    });
    $('chat-close').addEventListener('click', () => setOpen(false));

    // Clear = a fresh conversation: model history, attachments and the rendered
    // transcript go; the empty-state suggestions come back. Settings and the
    // working image are untouched. Disabled mid-turn, like attach.
    const clearBtn = $('chat-clear');
    clearBtn.addEventListener('click', () => {
      if (sending) return;
      // Shared state, shared repaint: the log + attachment listeners drop the
      // rendered rows in BOTH surfaces and bring each one's empty state back.
      clearSharedConversation(app);
      updateControls();
      input.focus();
    });

    // ── Docked-size resizer (wirePanelResizer-style, but axis-aware + float corner) ──
    const setSize = (px) => {
      const max = (dock === 'top' || dock === 'bottom' ? window.innerHeight : window.innerWidth) * DOCK_MAX_FRACTION;
      const clamped = Math.max(DOCK_MIN_SIZE, Math.min(Math.round(max), Math.round(px)));
      document.documentElement.style.setProperty('--chat-size', clamped + 'px');
    };
    // Window-level move/up listeners (pointer capture as an assist) so the resize keeps
    // tracking off the 16px handle. One gesture starter for every resize surface: the
    // docked-axis handle (doubles as the float SE grip) and the float-only handles.
    const beginResize = (handle, dir) => (e) => {
      e.preventDefault();
      e.stopPropagation();
      const startX = e.clientX, startY = e.clientY;
      const rect = host.getBoundingClientRect();
      const startW = rect.width, startH = rect.height;
      const start = { ...floatRect };
      adoptLayout();   // resizing the float = the user chose this shape
      try { handle.setPointerCapture(e.pointerId); } catch { /* capture is best-effort */ }
      handle.classList.add('dragging');
      // While a gesture is live, other handles' hover affordances are suppressed
      // (CSS gates on this class) so only the dragged handle shows as active.
      host.classList.add('chat-gesturing');
      trackPointer((ev) => {
        if (dock === 'float') {
          floatRect = resizeFloatRect(start, dir, ev.clientX - startX, ev.clientY - startY, window.innerWidth, window.innerHeight);
          applyFloatRect();
        } else if (dock === 'left') setSize(startW + (ev.clientX - startX));
        else if (dock === 'right') setSize(startW + (startX - ev.clientX));
        else if (dock === 'top') setSize(startH + (ev.clientY - startY));
        else setSize(startH + (startY - ev.clientY));
      }, () => {
        handle.classList.remove('dragging');
        host.classList.remove('chat-gesturing');
      });
    };
    const resizer = $('chat-resizer');
    resizer.addEventListener('pointerdown', beginResize(resizer, 'se'));
    for (const fh of host.querySelectorAll('.chat-float-handle')) {
      fh.addEventListener('pointerdown', beginResize(fh, fh.dataset.dir));
    }

    // ── Edge drop zones, shown only WHILE a header drag is live (no permanent DOM) ──
    let zonesEl = null;
    const showDockZones = () => {
      if (zonesEl) return;
      zonesEl = document.createElement('div');
      zonesEl.className = 'chat-dock-zones';
      for (const side of ['left', 'right', 'top', 'bottom']) {
        const z = document.createElement('div');
        z.className = `chat-dock-zone chat-dock-zone-${side}`;
        z.dataset.side = side;
        // Animated chevron pointing INTO the edge — the "dock here" affordance.
        const arrow = document.createElement('span');
        arrow.className = 'chat-zone-arrow';
        arrow.innerHTML = icon(`chevron-${side === 'top' ? 'up' : side === 'bottom' ? 'down' : side}`, { size: 18 });
        z.appendChild(arrow);
        zonesEl.appendChild(z);
      }
      document.body.appendChild(zonesEl);
    };
    const highlightDockZone = (side) => {
      if (!zonesEl) return;
      for (const z of zonesEl.children) z.classList.toggle('chat-dock-zone-active', z.dataset.side === side);
    };
    const hideDockZones = () => { zonesEl?.remove(); zonesEl = null; };

    // ── Header = drag handle (everywhere except the buttons). Floating: moves the
    // window. Docked: undocks into float AT THE POINTER and continues the same
    // gesture. Releasing over an edge zone docks there; elsewhere keeps floating.
    const header = $('chat-header');
    header.addEventListener('pointerdown', (e) => {
      if (e.target.closest('button')) return;
      // Phones present the panel as an ordinary modal (components.css) — no
      // dragging, no undocking, no dock zones.
      if (phoneModal()) return;
      e.preventDefault();
      try { header.setPointerCapture(e.pointerId); } catch { /* capture is best-effort */ }
      const downX = e.clientX, downY = e.clientY;
      let started = false;
      let offX = 0, offY = 0;
      const begin = (ev) => {
        started = true;
        adoptLayout();   // header drag = the user chose where this panel lives
        if (dock !== 'float') {
          // Undock: keep the stored float size, grabbed by the header at the pointer.
          floatRect = clampRect({ ...floatRect, x: ev.clientX - Math.min(floatRect.w / 2, 140), y: ev.clientY - 14 });
          setDock('float');
        }
        offX = ev.clientX - floatRect.x;
        offY = ev.clientY - floatRect.y;
        header.classList.add('chat-dragging');
        host.classList.add('chat-gesturing');
        showDockZones();
      };
      trackPointer((ev) => {
        if (!started) {
          if (Math.abs(ev.clientX - downX) < DRAG_THRESHOLD_PX && Math.abs(ev.clientY - downY) < DRAG_THRESHOLD_PX) return;
          begin(ev);
        }
        floatRect = clampRect({ ...floatRect, x: ev.clientX - offX, y: ev.clientY - offY });
        applyFloatRect();
        highlightDockZone(dockZoneAt(ev.clientX, ev.clientY, window.innerWidth, window.innerHeight));
      }, (ev) => {
        header.classList.remove('chat-dragging');
        host.classList.remove('chat-gesturing');
        hideDockZones();
        if (!started) return;
        const zone = ev.type === 'pointercancel' ? null
          : dockZoneAt(ev.clientX, ev.clientY, window.innerWidth, window.innerHeight);
        if (zone) setDock(zone);
      });
    });

    setDock(dock);
    updateControls();

    // ── Scripting surface: the stencil facade (console/stencilApi.js) drives the
    // panel through these — the SAME code paths as the buttons. `app.chat` is THIS
    // panel; §12 persistence wires later under `app.chatPersistence`.
    app.chat = {
      open: () => setOpen(true),
      close: () => setOpen(false),
      isOpen: () => panelIsOpen(),
      dock: (mode) => {
        const m = String(mode || '').toLowerCase();
        if (!DOCKS.includes(m)) throw new Error(`Unknown dock mode "${mode}" — one of ${DOCKS.join(', ')}`);
        adoptLayout();   // scripted dock = as deliberate as the dock buttons
        setDock(m);
      },
      // One programmatic turn through the panel's pipeline: shared history, the
      // exchange rendered in the transcript, typed LlmErrors rejecting through.
      prompt: async (text, images = []) => {
        if (sending) throw new Error('The assistant is already answering — wait for the current turn');
        // Check the WHOLE batch against the remaining room first: adding one at a time
        // would throw partway and leave the earlier ones queued, so a rejected call
        // would still have changed the tray (§7 MAX_ATTACHMENTS).
        const room = MAX_ATTACHMENTS - (peekChatController(app)?.attachments.length || 0);
        if (images.length > room) {
          throw new Error(`up to ${MAX_ATTACHMENTS} images per message${room < MAX_ATTACHMENTS ? ` (${room} slot${room === 1 ? '' : 's'} left)` : ''}`);
        }
        for (const u of images) ctrl().addImageDataUrl(u);
        renderAttachments();
        return runTurn(String(text ?? ''));
      },
      // Read-only transcript in the §12.1 display form: settled user/assistant
      // turns, text only — raw model JSON, error cards, and the in-flight "…"
      // row never appear. Fresh copies per read; mutating them changes nothing.
      history: () => rowsToMessages(chatLog()).map((m) => ({ role: m.role, text: m.text })),
      // Stop the in-flight turn — the Stop button's exact path. True when a
      // turn was actually running; a no-op false when idle.
      abort: () => {
        const had = !!turnAbort;
        turnAbort?.abort();
        return had;
      },
      // A fresh conversation — the trash button's exact shared path (§12: the
      // persisted copy clears with the emptied transcript). Refused mid-turn,
      // with words where the button simply disables.
      clear: () => {
        if (sending) throw new Error('The assistant is answering — stop the turn before clearing');
        clearSharedConversation(app);
        updateControls();
      },
      get isSending() { return sending; },
      controller: ctrl,
    };
  }
}
define('stencil-chat-panel', StencilChatPanel);
