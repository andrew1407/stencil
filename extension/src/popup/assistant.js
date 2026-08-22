// ── Embedded AI assistant section (llm-contract.md §8) ─────────────────
// The collapsed-by-default "Assistant" section of the popup / side panel / DevTools
// panel (all driven by popup.js), chatting about the surface's LIVE scan items.
// Boots lazily; conversation state lives with this document only — never persisted.
// src/llm/chatController owns history/listing/plan execution; this module wires the
// chrome capabilities (focus highlight, `#stencil=` hand-off, authed fetch + downscale).
// LLM output is DATA: everything the model produces renders via textContent, never innerHTML.
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, getSettings, buildHandoff, resumeInOpenEditor } from '../lib/stencil.js';
import { highlightSourceOnTab } from '../lib/hoverHighlight.js';
import { highlightColorValue } from '../lib/highlightColor.js';
import { sourceOf, editableSrc, hostLabel } from '../lib/imageModel.js';
import { formatOfItem } from '../lib/filters.js';
import { icon } from '../lib/icons.js';
import { guessKindFromUrl } from '../lib/dragUrl.js';
import { isAllowedImageUrl } from '../lib/urlGuard.js';
import { loadLlmSettings, LLM_SETTINGS_KEY } from '../llm/llmSettings.js';
import { createLlmClient, LlmError, probeProvider } from '../llm/llmClient.js';
import { serverTokenFor, turnFailureText } from '../llm/llmSurface.js';
import { loadConnections } from '../lib/connections.js';
import { createChatController, translateOpenActions, splitDataUrl, matchListingIndex, MAX_IMAGE_EDGE, MAX_ATTACHMENTS } from '../llm/chatController.js';
import { askAnswerText } from '../llm/opPlan.js';
import { wireDropTarget, isVideoFile } from '../lib/chatDrop.js';
import { rasterizeToPngDataUrl, decodeSize, fitSize } from '../lib/rasterize.js';
import { makeDismissible, renderSuggestions, wireThumbPreview, AUTO_DISMISS_MS } from '../lib/chatUi.js';
import { createMsgMenu, createMsgMenuButton, appendToPrompt } from '../lib/chatMsgMenu.js';
import { openPanelDialog } from './dialogShell.js';
import { MSG } from '../lib/messages.js';
import {
  observeReveal, leaveThenRemove, wipeDurationMs, CHAT_LEAVE_MS, scatterGridFor,
} from '../lib/motion.js';

// Every assistant entry leaves on the same dissolve. `count` is how many are going at
// once — one removal gets the full fine mesh, a whole-transcript clear coarsens so the
// total number of flying tiles stays inside the budget (lib/motion.js scatterGridFor).
const chatLeave = (el, done, count = 1, index = 0) =>
  leaveThenRemove(el, done, { ms: CHAT_LEAVE_MS, ...scatterGridFor(count, index) });

// Videos are never sent to the LLM (contract §7): dropped video files/URLs are
// decoded by a <video> element and sampled into JPEG frames, which attach as images.
const VIDEO_FRAME_COUNT = 4;
const VIDEO_TIMEOUT_MS = 8000;
const VIDEO_JPEG_QUALITY = 0.92;
const VIDEO_DECODE_ERROR = 'this video can’t be decoded here (unsupported codec?)';

// Show/hide the whole assistant surface — section AND the ✦ header button. Provider
// 'none' (contract §5) hides both; the elements stay in the DOM so re-enabling in
// Options needs no reload. A hidden section is not a collapsed one (drag spring skips it).
export const applyAssistantVisibility = (enabled, { section, button } = {}) => {
  const on = !!enabled;
  if (section) section.hidden = !on;
  if (button) button.hidden = !on;
  return on;
};

// A canvas sized so the long edge of w×h fits MAX_IMAGE_EDGE (contract §7 downscale,
// rasterize.js fitSize — never below 1×1).
const fitCanvas = (w, h) => {
  const s = fitSize(w, h, MAX_IMAGE_EDGE);
  const c = document.createElement('canvas');
  c.width = s.width || 1;
  c.height = s.height || 1;
  return c;
};

// Downscale + re-encode ANY image source as PNG → the LlmImage { mediaType, data }.
// lib/rasterize.js handles SVG (createImageBitmap rejects image/svg+xml) via an <img> +
// canvas fallback; `width`/`height` are the scan entry's dims, for sources declaring none.
const toLlmImage = async (source) => {
  const img = splitDataUrl(await rasterizeToPngDataUrl(source, { maxEdge: MAX_IMAGE_EDGE }));
  if (!img) throw new Error('could not encode the image');
  return img;
};

// Wait for one `type` event on `v`, bounded: the video erroring or the timeout
// elapsing rejects instead (a stuck decode must not hang the chat's send loop).
const nextVideoEvent = (v, type, timeoutMsg) => new Promise((resolve, reject) => {
  let done = false;
  const fin = (fn, x) => {
    if (done) return;
    done = true;
    clearTimeout(timer);
    v.removeEventListener(type, ok);
    v.removeEventListener('error', err);
    fn(x);
  };
  const ok = () => fin(resolve);
  const err = () => fin(reject, new Error(VIDEO_DECODE_ERROR));
  const timer = setTimeout(() => fin(reject, new Error(timeoutMsg)), VIDEO_TIMEOUT_MS);
  v.addEventListener(type, ok);
  v.addEventListener('error', err);
});

// `count` evenly-spaced JPEG frames (first included) of a video blob. ONE <video>
// decodes the whole pass — loaded once, then seeked sequentially (the pattern
// browser/js/core/videoFrame.js uses) — not re-decoding the blob per frame.
const sampleVideoFrames = async (blob, count = VIDEO_FRAME_COUNT) => {
  const url = URL.createObjectURL(blob);
  const v = document.createElement('video');
  v.muted = true; v.preload = 'auto';
  try {
    const loaded = nextVideoEvent(v, 'loadeddata', 'video metadata timeout');
    v.src = url;
    await loaded;
    const duration = Number.isFinite(v.duration) ? v.duration : 0;
    const out = [];
    for (let i = 0; i < count; i++) {
      const seeked = nextVideoEvent(v, 'seeked', 'video frame timeout');
      try { v.currentTime = Math.min((duration * i) / count, Math.max(0, duration - 0.01)); }
      catch { throw new Error('video seek failed'); }
      await seeked;
      try {
        const c = fitCanvas(v.videoWidth, v.videoHeight);
        c.getContext('2d').drawImage(v, 0, 0, c.width, c.height);
        out.push(c.toDataURL('image/jpeg', VIDEO_JPEG_QUALITY));
      } catch { throw new Error('video frame capture failed'); }
    }
    return out;
  } finally {
    v.removeAttribute('src');   // release the decoder before the URL goes away
    URL.revokeObjectURL(url);
  }
};

const entryName = (entry) => (entry && entry.name) || 'image';

// Build the assistant for this surface. The injected accessors are live, per call:
// getItems/getTabId/getPageUrl expose popup.js's current scan (the §8 listing source);
// openHere imports into the editor tab the panel stands on (false = classic new-tab
// hand-off stays in charge); pinImage/unpinImage/rescan/setTheme/setFilters/setAccent
// wire §8 ops to the host's own controls (absent = "not supported here").
// Returns { handleToggle(collapsed), reveal() } — boot the chat UI on first expansion,
// and the ✦ header button's expand + scroll + focus.
export const createAssistant = ({ getItems, getTabId, getPageUrl, openHere = () => false,
                                 pinImage: pinEntry, unpinImage: unpinEntry,
                                 rescan: rescanPage, setTheme, setFilters, setAccent }) => {
  const sectionEl = document.getElementById('sec-assistant');
  const transcriptEl = document.getElementById('chat-transcript');
  const inputEl = document.getElementById('chat-input');
  const sendBtn = document.getElementById('chat-send');
  const trayEl = document.getElementById('chat-tray');

  // Transcript entries fade + lift in as they arrive and dissolve at the top edge
  // as the conversation scrolls past them (browser parity, css/animations.css).
  observeReveal(transcriptEl, ':scope > *');

  let booted = false;
  let busy = false;
  let turnAbort = null;
  const clearBtn = document.getElementById('chat-clear');
  let llmSettings = null;

  // General (non-LLM) extension settings, read repeatedly by focus/open — cached
  // and invalidated when they change (they live in chrome.storage.sync).
  let probeGen = 0;   // drops stale async provider probes
  let settingsPromise = null;
  const cachedSettings = () => (settingsPromise ||= getSettings());

  // ── Transcript rendering (textContent only — LLM output is data) ──────────
  // Clear = fresh conversation: model history + transcript + queued attachments.
  // The empty-state suggestion chips come back (browser parity).
  const clearConversation = () => {
    if (busy) return;
    controller.clearConversation();
    workingScan = null;   // back to chatting about the panel's own scan
    // Scatter the entries out, then wipe (leaveThenRemove always calls back). Entries
    // are removed individually, NOT by wiping textContent: the scatter's particle layer
    // is itself a child of the transcript, and a wholesale wipe would kill the particles.
    const going = [...transcriptEl.children].filter((el) => !el.classList.contains('disintegrate-host'));
    const wiped = going.length > 0;
    going.forEach((el, i) => chatLeave(el, () => el.remove(), going.length, i));
    pending.splice(0);
    renderTray();
    // The empty state comes back only once the entries have finished leaving —
    // prepended in the same tick, the chips would sit under the still-falling particles
    // (browser chatView.js restoreEmptyState sequences it the same way).
    if (wiped) {
      setTimeout(() => {
        // A turn may have started while the wipe played — then the chips are wrong.
        // `:scope >` like syncClearBtn: the scatter's clones keep the .msg class, so
        // an unscoped query would read the dying copies as live conversation.
        if (busy || transcriptEl.querySelector(':scope > .msg, :scope > .card, :scope > .warn')) return;
        showSuggestions();
        syncClearBtn();
      }, wipeDurationMs());
    } else {
      showSuggestions();
    }
    syncClearBtn();
    inputEl.focus();
  };

  // Pin now and again after the entrance animation settles — a fresh row (the
  // pending indicator included) grows AFTER the first measure, so one write landed
  // a row short of the bottom (browser chatView stickToBottom parity).
  const scrollDown = () => {
    const pin = () => { transcriptEl.scrollTop = transcriptEl.scrollHeight; };
    pin();
    if (typeof requestAnimationFrame === 'function') requestAnimationFrame(pin);
    setTimeout(pin, 220);
  };

  const appendDiv = (className, text) => {
    const div = document.createElement('div');
    div.className = className;
    div.textContent = text;
    transcriptEl.appendChild(div);
    syncClearBtn();
    scrollDown();
    return div;
  };

  // Side-notes (warnings) stay DISMISSABLE; error MESSAGES carry no × — the browser
  // panel renders a failed turn as a plain bubble with the retry only, and the three
  // surfaces read the same.
  const dismissible = (el, autoMs = 0) => {
    makeDismissible(el, { doc: document, autoMs });
    return el;
  };

  // What the per-message context menu acts on: the message's original text (and, for
  // user turns, the attachments that rode it — kept so Resend can requeue them).
  const msgMeta = new WeakMap();
  let addMsgMenuBtn = () => {};   // wired at boot, once the message menu exists
  const addMsg = (cls, text) => {
    const el = appendDiv(`msg ${cls}`, text);
    msgMeta.set(el, { text });
    if (cls !== 'note') addMsgMenuBtn(el);   // notes (and the typing row) get no menu
    return el;
  };
  const addWarn = (text) => dismissible(appendDiv('warn', text));

  // The images riding the user's turn, as a thumbnail strip on the user's side of
  // the transcript (browser chatView.js chatAttachmentStrip parity). Names come from
  // scanned pages, so they stay DATA — alt/title only, never markup.
  const addAttachments = (list) => {
    const strip = document.createElement('div');
    strip.className = 'chat-attached';
    for (const p of list) {
      const img = document.createElement('img');
      img.className = 'chat-attached-thumb';
      img.src = `data:${p.image.mediaType};base64,${p.image.data}`;
      img.alt = p.name;
      img.title = Number.isInteger(p.index) ? `image #${p.index} from the page (${p.name})` : p.name;
      // 56px is too small to tell two screenshots apart — hovering shows it big.
      wireThumbPreview(img, { caption: img.title });
      strip.appendChild(img);
    }
    transcriptEl.appendChild(strip);
    syncClearBtn();
    scrollDown();
    return strip;
  };

  // Icon-only Retry inside a bubble: re-sends the SAME turn through the normal path
  // — its attachments included (nothing is auto-retried). Failed and stopped turns.
  const addRetry = (el, text, send, attachments = []) => {
    const retry = document.createElement('button');
    retry.className = 'chat-retry';
    retry.title = 'Send this message again';
    retry.setAttribute('aria-label', 'Retry');
    retry.innerHTML = icon('refresh', { size: 13 });
    retry.addEventListener('click', () => { if (!busy) { el.remove(); send(text, attachments); } });
    el.appendChild(retry);
  };

  // The in-flight turn's placeholder: an assistant BUBBLE holding three bouncing dots
  // — the shape the reply will take, in the place it will take it (browser chatView.js
  // typingDots).
  const addThinking = () => {
    const el = appendDiv('msg assistant typing-row', '');
    const dots = document.createElement('span');
    dots.className = 'chat-typing';
    dots.setAttribute('role', 'status');
    dots.setAttribute('aria-label', 'Assistant is answering');
    for (let i = 0; i < 3; i++) dots.appendChild(document.createElement('i'));
    el.appendChild(dots);
    scrollDown();
    return el;
  };

  // One executed-action result card. `label` is plain text (entry names come from
  // scanned pages, so they're untrusted too). Failure cards carry a × and, when
  // `autoMs` is set (attach failures), clear themselves after a few seconds.
  const addCard = (iconName, label, ok = true, autoMs = 0) => {
    const div = document.createElement('div');
    div.className = 'card' + (ok ? '' : ' fail');
    div.innerHTML = icon(ok ? iconName : 'x', { size: 14 });   // fixed glyph, no user data
    const span = document.createElement('span');
    span.textContent = label;
    div.appendChild(span);
    if (!ok) makeDismissible(div, { doc: document, autoMs });
    transcriptEl.appendChild(div);
    syncClearBtn();
    scrollDown();
    return div;
  };

  // ── Empty-state suggestion chips (browser parity, §8 wording) ─────────────
  // Clicking one PREFILLS the input — it never sends. The block disappears with the
  // first message and returns when the conversation is cleared.
  let suggestEl = null;
  const hideSuggestions = () => {
    suggestEl?.remove(); suggestEl = null;
  };
  // Just the chips (browser chatEmptyState parity) — no lead-in note repeating them.
  const showSuggestions = () => {
    hideSuggestions();
    suggestEl = renderSuggestions(document, (prompt) => {
      inputEl.value = prompt;
      inputEl.focus();
    });
    transcriptEl.appendChild(suggestEl);
    scrollDown();
  };

  // One renderer per executed-action card kind, keyed like the controller's op
  // executors (and opPlan.js's validators).
  const cardRenderers = {
    focus: (c) => addCard('pin', c.ok
      ? `Focused image ${c.index} (${entryName(c.entry)}) on the page`
      : `Image ${c.index} (${entryName(c.entry)}) was not found on the page`, c.ok),
    open: (c) => addCard('pencil', c.ok
      ? `Opened image ${c.index} (${entryName(c.entry)}) in the editor`
      : `Could not open image ${c.index} (${entryName(c.entry)})`, c.ok),
    attach: (c) => {
      const got = c.attached.length;
      addCard('sparkle', got
        ? `Attached image${got > 1 ? 's' : ''} ${c.attached.join(', ')} for analysis`
        : `Could not attach image${c.indices.length > 1 ? 's' : ''} ${c.indices.join(', ')}`,
      got > 0, AUTO_DISMISS_MS);
    },
    // The glyph says which setting moved: the theme's own sun/moon/monitor (the same
    // three Options offers), and the panel icon for the list the filters govern.
    theme: (c) => addCard({ light: 'sun', dark: 'moon', system: 'monitor' }[c.mode] || 'moon',
      c.ok ? `Switched the panel to the ${c.mode} theme`
           : `Could not switch to the ${c.mode} theme`, c.ok),
    filter: (c) => addCard('sidebar', c.ok
      ? (c.applied.length ? `Filters: ${c.applied.join(' · ')}` : 'Filters unchanged')
      : 'Could not change the filters', c.ok),
    pin: (c) => {
      const got = c.pinned.length;
      addCard('pin', got
        ? `Pinned image${got > 1 ? 's' : ''} ${c.pinned.join(', ')}`
        : `Could not pin image${c.indices.length > 1 ? 's' : ''} ${c.indices.join(', ')}`,
      got > 0);
    },
    unpin: (c) => {
      const got = c.unpinned.length;
      addCard('pin', got
        ? `Unpinned image${got > 1 ? 's' : ''} ${c.unpinned.join(', ')}`
        : `Could not unpin image${c.indices.length > 1 ? 's' : ''} ${c.indices.join(', ')}`,
      got > 0);
    },
    scanTab: (c) => addCard('monitor', c.ok
      ? `Scanned tab ${c.index}${c.title ? ` (${c.title})` : ''} — now chatting about its ${c.count} image${c.count === 1 ? '' : 's'}`
      : `Could not scan tab ${c.index}${c.title ? ` (${c.title})` : ''}`, c.ok),
    rescan: (c) => addCard('refresh', c.ok
      ? `Re-scanned the page — the listing now shows ${c.count} image${c.count === 1 ? '' : 's'}`
      : 'Could not re-scan the page', c.ok),
    accent: (c) => addCard('gear', c.ok
      ? `Accent set to ${c.applied}${c.exact ? '' : ` (the nearest preset to ${c.asked})`}`
      : `Could not set the accent to ${c.asked}`, c.ok),
    openUrl: (c) => addCard('pencil', c.ok
      ? `Opened ${c.url} in ${c.incognito ? 'a new incognito editor' : 'the editor'}`
      : `Could not open ${c.url}`, c.ok),
  };

  const renderCards = (cards) => {
    for (const c of cards) cardRenderers[c.kind]?.(c);
  };

  // ── §11 choice card ────────────────────────────────────────────────────────
  // The model's `ask` rendered under its reply: radios (single) / checkboxes (multi) +
  // an optional free-text row. Submitting sends the answer as the user's NEXT turn —
  // nothing applies on click; answered cards lock. Model text is DATA: textContent throughout.
  const renderAsk = (ask) => {
    const wrap = document.createElement('div');
    wrap.className = 'chat-ask';
    const q = document.createElement('div');
    q.className = 'chat-ask-q';
    q.textContent = ask.question;
    wrap.appendChild(q);

    const name = `ask-${Math.random().toString(36).slice(2)}`;
    const multi = ask.mode === 'multi';
    const inputs = [];
    const list = document.createElement('div');
    list.className = 'chat-ask-options';
    ask.options.forEach((opt, i) => {
      const row = document.createElement('label');
      row.className = 'chat-ask-option';
      const box = document.createElement('input');
      box.type = multi ? 'checkbox' : 'radio';
      box.name = name;
      inputs.push(box);
      row.appendChild(box);
      // Only images the page scan already surfaced (the validator drops a model-supplied
      // `image.url`). A scan entry with no usable src collapses to a label-only row.
      const src = opt.image?.scanIndex != null
        ? editableSrc((getItems() || [])[opt.image.scanIndex] || {})
        : '';
      if (src) {
        const img = document.createElement('img');
        img.className = 'chat-ask-thumb';
        img.alt = '';
        img.src = src;
        img.addEventListener('error', () => img.remove());
        row.appendChild(img);
      }
      const label = document.createElement('span');
      label.className = 'chat-ask-label';
      label.textContent = opt.label;
      row.appendChild(label);
      list.appendChild(row);
    });
    wrap.appendChild(list);

    let customBox = null;
    let customText = null;
    if (ask.allowCustom) {
      const row = document.createElement('label');
      row.className = 'chat-ask-option';
      customBox = document.createElement('input');
      customBox.type = multi ? 'checkbox' : 'radio';
      customBox.name = name;
      inputs.push(customBox);
      customText = document.createElement('input');
      customText.type = 'text';
      customText.className = 'chat-ask-custom-text';
      customText.placeholder = ask.customLabel;
      customText.addEventListener('input', () => { customBox.checked = true; sync(); });
      row.append(customBox, customText);
      wrap.appendChild(row);
    }

    const actions = document.createElement('div');
    actions.className = 'chat-ask-actions';
    const submit = document.createElement('button');
    submit.type = 'button';
    submit.className = 'chat-ask-submit';
    submit.textContent = 'Submit';
    actions.appendChild(submit);
    wrap.appendChild(actions);

    const chosen = () => ask.options.filter((_, i) => inputs[i]?.checked);
    const typed = () => (customBox?.checked ? customText.value.trim() : '');
    function sync() { submit.disabled = !chosen().length && !typed(); }
    for (const b of inputs) b.addEventListener('change', sync);
    sync();

    let answered = false;
    submit.addEventListener('click', () => {
      // The flag, not the DOM, is what makes "answered once" true: a removed button keeps
      // its listener, so a retained reference could otherwise re-send the turn.
      if (answered) return;
      const answer = askAnswerText(ask, { picked: chosen(), custom: typed() });
      if (!answer) return;
      answered = true;
      for (const b of inputs) b.disabled = true;
      if (customText) customText.disabled = true;
      submit.remove();
      const sent = document.createElement('div');
      sent.className = 'chat-ask-sent';
      sent.textContent = answer;
      actions.appendChild(sent);
      wrap.classList.add('chat-ask-answered');
      send(answer);
    });
    transcriptEl.appendChild(wrap);
    syncClearBtn();
    scrollDown();
  };

  const renderResult = (result) => {
    renderCards(result.cards);
    addMsg('assistant', result.reply);
    // A "chat-only" reply that LOOKS like a plan is the model mangling its JSON
    // (§1 extraction found no parseable object). The raw text still shows above
    // (it is the reply — data, never executed); this note says why nothing ran.
    if (result.chatOnly && /^\s*[{`]/.test(result.reply || '') && /"(op|actions|version)"/.test(result.reply || '')) {
      addWarn('That answer looks like a plan, but its JSON is malformed — nothing was executed. Small models often mangle plan JSON; try again or switch to a larger model in Options.');
    } else if (result.chatOnly && /^\s*</.test(result.reply || '') && /<\w+[\s>]/.test(result.reply || '')) {
      addWarn('The model answered with markup instead of a Stencil plan — nothing was executed. Try again or switch to a larger model in Options.');
    }
    for (const w of result.warnings) addWarn(w);
    if (result.ask) renderAsk(result.ask);
    if (result.continuation) {
      // attach and/or scanTab gathered context — the wording covers both.
      addMsg('note', 'Continued automatically with the gathered context…');
      renderResult(result.continuation);
    }
    // §10 clearChat rides only the OUTERMOST result and resolved after everything
    // else, so its note lands last. Confirmed = the wipe itself is the feedback
    // (it runs once busy drops); declined = the contract's "clear canceled" note.
    if (result.clearChat && !result.clearChat.confirmed) {
      addMsg('note', 'Clear canceled — the conversation stays.');
    }
  };

  // ── Settings / provider line ───────────────────────────────────────────────
  const refreshSettings = async () => {
    llmSettings = await loadLlmSettings();
    const s = llmSettings;
    // No provider line in the panel (browser parity): reachability lives on the …
    // trigger's dot + rich tooltip. Amber while probing, green connected, red
    // unreachable; generation-guarded so a slow probe can't paint over a newer one.
    const dot = document.getElementById('chat-status-dot');
    const moreBtn = document.getElementById('chat-more-btn');
    if (!dot || !moreBtn) return;
    const gen = ++probeGen;
    dot.className = 'chat-status-dot';
    moreBtn.title = 'More — attach, clear, settings\nChecking the configured LLM…';
    const probe = await probeProvider(s, {
      getToken: async (u) => serverTokenFor(u, { connections: await loadConnections(), settings: s }),
    });
    if (gen !== probeGen) return;
    dot.className = `chat-status-dot ${probe.ok ? 'ok' : 'bad'}`;
    const status = probe.ok
      ? `Connected${probe.detail ? ` — ${probe.detail}` : ''}`
      : (probe.detail || 'Unreachable');
    moreBtn.title = [
      'More — attach, clear, settings',
      `Provider: ${s.provider}`,
      probe.url ? `Endpoint: ${probe.url.replace(/^https?:\/\//i, '')}` : '',
      `Model: ${s.model || 'server default'}`,
      `Status: ${status}`,
    ].filter(Boolean).join('\n');
  };

  // ── Injected controller capabilities ───────────────────────────────────────

  // A scanTab op swapped the conversation's working set to ANOTHER tab's images
  // (contract §8): { tabId, url, title, items }. Null = the popup's own scan is
  // the working set. Cleared with the conversation.
  let workingScan = null;

  // §10 clearChat: the user confirmed mid-turn, but the transcript is torn down only
  // AFTER the turn has rendered and busy dropped — through the one existing clear flow.
  let wipeAfterTurn = false;

  // `clearChat`'s in-panel confirm (dialogShell.js — the same yes/no shape as the
  // editor-mode close confirm; click-away, Escape and Cancel all mean no).
  const confirmClearChat = () => openPanelDialog({
    build: (finish) => {
      const title = document.createElement('div');
      title.className = 'dialog-title';
      title.textContent = 'Clear this conversation?';
      const what = document.createElement('div');
      what.className = 'dialog-note';
      what.textContent = 'The assistant asked to clear the chat. The transcript and its history go away.';
      const row = document.createElement('div');
      row.className = 'dialog-actions';
      const cancel = document.createElement('button');
      cancel.textContent = 'Cancel';
      cancel.addEventListener('click', () => finish(false));
      const ok = document.createElement('button');
      ok.className = 'primary';
      ok.textContent = 'Clear';
      ok.addEventListener('click', () => finish(true));
      row.append(cancel, ok);
      return [title, what, row];
    },
  }).then((v) => v === true);

  // `focus`: the existing injected page marking (hoverHighlight.js) — outline + scroll
  // into view; true when found. The target tab is the entry's own provenance
  // (sourceTabId) when it has one — a scanTab'd entry lives on that other tab.
  const focusImage = async (index, entry) => {
    const src = sourceOf(entry) || (entry && entry.src) || '';
    const tabId = (entry && entry.sourceTabId != null) ? entry.sourceTabId : getTabId();
    if (!src || tabId == null) return false;
    const color = await highlightColorValue(await cachedSettings());
    return highlightSourceOnTab(tabId, src, color);
  };

  // `open`: the `#stencil=` editor hand-off, with validated open.actions translated
  // onto launch options. In EDITOR mode the image imports INTO that tab (openHere,
  // contract §8) — except a plan that also asked for a filter/layout: the import
  // message can't carry annotations, so only the fragment path will do.
  const openImage = async (action, entry) => {
    const src = editableSrc(entry);
    if (!src) throw new Error('this entry has no openable image source');
    // §8 open.mode "resume": focus the editor tab ALREADY holding this image instead of
    // handing off a fresh copy. Requested edits need a fresh hand-off, so they win over
    // resume; with no open editor holding it, the classic hand-off carries open:'resume'.
    const resume = action.mode === 'resume' && !(action.actions && action.actions.length);
    const extra = [];
    if (action.mode === 'resume' && !resume) {
      extra.push('The requested edits need a fresh editor hand-off — opened a copy instead of resuming');
    }
    if (resume && await resumeInOpenEditor({ source: sourceOf(entry), name: entry && entry.name })) {
      return extra;
    }
    const { page } = await cachedSettings();
    const dataUrl = await fetchAsDataUrl(src, { pageUrl: (entry && entry.resource) || getPageUrl() });
    let width = entry.w || 0, height = entry.h || 0;
    if (!(width > 0 && height > 0)) {
      // Same two-step decode as the attach path (an SVG can't go through
      // createImageBitmap) — unknown dims only mean the crop translation warns.
      try {
        ({ width, height } = await decodeSize({ dataUrl }));
      } catch { /* dims stay unknown — crop translation will warn */ }
    }
    const { launch, warnings: translated } = translateOpenActions(action.actions, { width, height });
    const warnings = extra.concat(translated);
    if (!launch.layout) {
      const landed = await openHere(entry, {
        incognito: !!action.incognito,
        crop: launch.crop || null,
        page: launch.page ? launch.page.size : '',
      });
      if (landed) return warnings;
    }
    const payload = buildHandoff(entry, {
      dataUrl, page, resource: (entry && entry.resource) || getPageUrl(),
      incognito: !!action.incognito, open: resume ? 'resume' : undefined,
    });
    if (launch.page) payload.page = launch.page;
    if (launch.crop) payload.crop = launch.crop;
    if (launch.layout) payload.layout = launch.layout;
    await openEditorTab(payload);
    return warnings;
  };

  // `attach`: fetch bytes through the extension's host permissions (cross-origin /
  // hotlink-protected sources work like the popup's thumbnails), rasterise + downscale.
  // SVG rasterises to PNG here — the contract accepts png/jpeg/webp/gif only.
  const attachImage = async (index, entry) => {
    const src = editableSrc(entry);
    if (!src) {
      throw new Error(entry && entry.kind === 'video'
        ? 'this video has no captured frame yet — play it on the page, then rescan (or drop the video onto the chat to sample frames)'
        : 'no fetchable image source');
    }
    const dataUrl = await fetchAsDataUrl(src, { pageUrl: (entry && entry.resource) || getPageUrl() });
    return toLlmImage({ dataUrl, width: (entry && entry.w) || 0, height: (entry && entry.h) || 0 });
  };

  // `getTabs`: the user's other open http(s) pages (SW SOURCE_TABS). Best-effort: []
  // on any failure. Gated at the SOURCE on the §8 opt-in, so a caller that forgets
  // the check still gets nothing.
  const getTabs = async () => {
    if (!llmSettings || llmSettings.shareTabs !== true) return [];
    try {
      const res = await chrome.runtime.sendMessage({ type: MSG.SOURCE_TABS });
      return (res && res.ok && Array.isArray(res.tabs)) ? res.tabs : [];
    } catch {
      return [];
    }
  };

  // `scanTab`: scan another open tab (SW SCAN_TAB) and make its images the working
  // set. Items get the same shaping as popup.js scan(): name, measured dims, per-row
  // provenance (sourceTabId + resource) so focus/open/pin work on them unchanged.
  const scanTab = async (tabEntry) => {
    if (!tabEntry || typeof tabEntry.tabId !== 'number') return { ok: false, error: 'no such tab' };
    const res = await chrome.runtime.sendMessage({ type: MSG.SCAN_TAB, tabId: tabEntry.tabId });
    if (!res || !res.ok) return { ok: false, error: (res && res.error) || 'the scan failed' };
    const items = (res.images || []).map((it) => ({
      ...it,
      sourceTabId: res.tabId,
      resource: res.url || '',
      name: filenameFromUrl(it.kind === 'video' && it.videoUrl ? it.videoUrl : it.src, it.kind === 'video' ? 'video' : 'image'),
      measured: it.w > 0 && it.h > 0,
    }));
    workingScan = { tabId: res.tabId, url: res.url || '', title: tabEntry.title || '', items };
    return { ok: true, count: items.length, title: workingScan.title };
  };

  // `pin`: popup.js's own pin path when injected (persists + re-sorts the list).
  // Entries from a scanTab'd working set pin by their own site (rowResource).
  const pinImage = async (index, entry) => {
    if (!pinEntry) throw new Error('pinning is not available on this surface');
    if (!entry) throw new Error('no such listing entry');
    await pinEntry(entry);
  };

  // `unpin` (§8): the same path in reverse — local pins only, like the row's Unpin.
  const unpinImage = async (index, entry) => {
    if (!unpinEntry) throw new Error('unpinning is not available on this surface');
    if (!entry) throw new Error('no such listing entry');
    await unpinEntry(entry);
  };

  // `rescan` (§8): refresh the conversation's CURRENT listing — the swapped tab while
  // a scanTab swap is live (the same SW scanner keeps the provenance shaping), else
  // the popup's own scan, which the injected getListing already rides.
  const rescanListing = async () => {
    if (workingScan) return scanTab({ tabId: workingScan.tabId, title: workingScan.title });
    if (!rescanPage) return { ok: false, error: 're-scanning is not available on this surface' };
    await rescanPage();
    return { ok: true, count: (getItems() || []).length };
  };

  // `openUrl`: a USER-GIVEN image URL (executor-guarded) → the same editor hand-off
  // as `open`, on a synthesized entry. Editor mode imports into the current editor
  // tab unless incognito was asked for (that always needs a new incognito editor).
  const openUrlImage = async (action) => {
    const url = action.url;
    const dataUrl = await fetchAsDataUrl(url, { pageUrl: getPageUrl() });
    const entry = { kind: 'img', src: url, name: filenameFromUrl(url, 'image'), w: 0, h: 0 };
    const { page } = await cachedSettings();
    if (!action.incognito) {
      const landed = await openHere(entry, { incognito: false, crop: null, page: '' });
      if (landed) return;
    }
    const payload = buildHandoff(entry, { dataUrl, page, resource: getPageUrl(), incognito: !!action.incognito });
    await openEditorTab(payload);
  };

  const controller = createChatController({
    getClient: () => createLlmClient({ settings: llmSettings }),
    // The working set: the scanTab'd tab's images while a swap is live, else the
    // popup's live scan items (contract §8).
    getListing: () => (workingScan ? workingScan.items : (getItems() || [])),
    formatOfItem,
    focusImage,
    openImage,
    attachImage,
    pageUrl: () => (workingScan ? workingScan.url : (getPageUrl() || '')),
    getTabs,
    scanTab,
    pinImage,
    unpinImage,
    rescan: rescanListing,
    openUrlImage,
    // Panel settings (§8): the host wires these to its own controls, so a plan that
    // changes the theme, the accent or the filters goes through the very same path a
    // click does.
    setTheme: setTheme ? async (mode) => setTheme(mode) : undefined,
    setFilters: setFilters ? async (patch) => setFilters(patch) : undefined,
    setAccent: setAccent ? async (action) => setAccent(action) : undefined,
    // §10 clearChat (deferred by the controller to the end of the turn): confirm
    // in-panel; on Yes the wipe waits until this turn has rendered.
    clearChat: async () => {
      const ok = await confirmClearChat();
      if (ok) wipeAfterTurn = true;
      return ok;
    },
  });

  // ── Pending dropped attachments (ride the next send) ──────────────────────
  // Each entry: { image: { mediaType, data }, name, index? } — `index` set when the
  // dropped URL matched a scan entry (the model can then focus/open it by index).
  const pending = [];

  const renderTray = () => {
    trayEl.innerHTML = '';
    trayEl.hidden = pending.length === 0;
    pending.forEach((p, i) => {
      const chip = document.createElement('div');
      chip.className = 'chip';
      const img = document.createElement('img');
      img.alt = '';
      img.src = `data:${p.image.mediaType};base64,${p.image.data}`;
      wireThumbPreview(img, { caption: p.name });   // the chip's 28px thumb, shown big on hover
      const name = document.createElement('span');
      name.className = 'chip-name';
      const label = Number.isInteger(p.index) ? `image #${p.index} from the page (${p.name})` : p.name;
      name.textContent = label;
      name.title = label;   // the chip ellipsises it; the full name lives on the tooltip
      const x = document.createElement('button');
      x.className = 'chip-x';
      x.textContent = '×';
      x.title = 'Remove';
      // The chip scatters before the tray rebuilds without it.
      x.addEventListener('click', () => chatLeave(x.parentElement, () => {
        pending.splice(i, 1); renderTray(); syncClearBtn();
      }));
      // Thumbnail + name + remove — no "analyze" badge: every queued image is
      // analysed, so it would say the same thing on every chip (browser parity).
      chip.append(img, name, x);
      trayEl.appendChild(chip);
    });
  };

  const addPending = (p) => {
    // One message carries at most MAX_ATTACHMENTS images (chatController.js) — the
    // extra is refused out loud rather than queued and silently dropped later.
    if (pending.length >= MAX_ATTACHMENTS) {
      addWarn(`Up to ${MAX_ATTACHMENTS} images per message — remove one first.`);
      return;
    }
    pending.push(p);
    renderTray();
    syncClearBtn();
  };

  // Pull a media URL and queue evenly-spaced frames from it (videos never go to the
  // LLM — contract §7). Same scheme allowlist as fetchAsDataUrl: host permissions make
  // fetch() all-powerful, so only http(s)/blob/data media URLs are pulled.
  const addPendingVideoUrl = async (url, baseName = '', pageUrl = '') => {
    if (!/^(https?|blob|data):/i.test(url)) throw new Error('unsupported URL scheme');
    // Scanned/dropped media URL — same SSRF guard as fetchAsDataUrl (urlGuard.js),
    // with the same scanned-page same-host carve-out.
    if (!isAllowedImageUrl(url, { allowSameHostAs: pageUrl })) throw new Error('blocked private or internal address');
    const resp = await fetch(url);
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const frames = await sampleVideoFrames(await resp.blob());
    const base = baseName || filenameFromUrl(url, 'video');
    frames.forEach((f, i) => addPending({ image: splitDataUrl(f), name: `${base} — frame ${i + 1}` }));
  };

  // A dropped URL: when it matches a scan entry, register the attachment AS that
  // entry (focus/open by index stays possible); otherwise fetch + attach it as a
  // plain vision image (analysis only). Video URLs are frame-sampled like files.
  const addPendingUrl = async (url) => {
    const items = getItems() || [];
    const idx = matchListingIndex(items, url);
    if (idx >= 0) {
      const entry = items[idx];
      // A video row attaches its captured still (which keeps the listing index, so
      // focus/open still work on it); with no still at all, sample the media itself.
      if (entry.kind === 'video' && !editableSrc(entry) && entry.videoUrl) {
        await addPendingVideoUrl(entry.videoUrl, entryName(entry), entry.resource || getPageUrl());
        return;
      }
      addPending({ image: await attachImage(idx, entry), name: entryName(entry), index: idx });
      return;
    }
    if (guessKindFromUrl(url) === 'video') {
      await addPendingVideoUrl(url, '', getPageUrl());
      return;
    }
    const dataUrl = await fetchAsDataUrl(url, { pageUrl: getPageUrl() });
    addPending({ image: await toLlmImage({ dataUrl }), name: filenameFromUrl(url) });
  };

  // A dropped/pasted local file: images attach directly; videos are sampled into frames.
  const addPendingFile = async (file) => {
    if (isVideoFile(file)) {
      const frames = await sampleVideoFrames(file);
      const base = (file.name || 'video').replace(/\.[^.]+$/, '');
      frames.forEach((f, i) => addPending({ image: splitDataUrl(f), name: `${base} — frame ${i + 1}` }));
      return;
    }
    if (!(file.type || '').startsWith('image/')) throw new Error(`not an image or video (got "${file.type || 'unknown'}")`);
    addPending({ image: await toLlmImage({ blob: file }), name: file.name || 'image' });
  };

  // Nothing said yet and nothing queued → there is nothing to clear, so the bin is disabled
  // rather than offering an action that would visibly do nothing. Re-checked wherever the
  // transcript or the attachment tray changes; `busy` still wins (no clearing mid-turn).
  const syncClearBtn = () => {
    // `:scope >` so the scatter's cloned entries (nested inside .disintegrate-host)
    // don't read as live transcript content and keep Clear enabled after a clear.
    clearBtn.disabled = busy
      || (!transcriptEl.querySelector(':scope > .msg, :scope > .warn, :scope > .card') && !pending.length);
  };

  // ── Send loop ──────────────────────────────────────────────────────────────
  const setBusy = (b) => {
    busy = b;
    // While a turn runs the send button IS the stop button (browser parity).
    sendBtn.disabled = false;
    sendBtn.innerHTML = icon(b ? 'stop' : 'send', { size: 15 });
    sendBtn.title = b ? 'Stop the response' : 'Send';
    inputEl.disabled = b;
    syncClearBtn();
  };

  // `preset` is an answer submitted by a §11 choice card rather than typed — it must not
  // consume (or clear) whatever the user has half-written in the composer.
  const send = async (preset, requeue = []) => {
    if (busy) { turnAbort?.abort(); return; }   // send button = STOP mid-turn
    const typedIn = preset != null;
    const text = typedIn ? String(preset).trim() : inputEl.value.trim();
    // A retry re-queues its turn's drained attachments (never over new ones).
    if (!pending.length && requeue.length) pending.push(...requeue);
    if (!text && !pending.length) return;
    hideSuggestions();
    if (!typedIn) inputEl.value = '';
    const attachments = pending.splice(0);
    renderTray();
    // The attached images belong to the USER's turn: thumbnails on the user's side,
    // above their message (assistant-side cards would read as if the assistant said it).
    if (attachments.length) addAttachments(attachments);
    const userEl = addMsg('user', text || '(attached images)');
    // Resend re-sends the ORIGINAL turn — its typed text (not the placeholder) and
    // its drained attachments, which stay reachable here exactly like Retry's.
    msgMeta.set(userEl, { text: text || '(attached images)', resendText: text, attachments });
    setBusy(true);
    const thinking = addThinking();
    try {
      turnAbort = typeof AbortController !== 'undefined' ? new AbortController() : null;
      const result = await controller.send(text, { attachments, signal: turnAbort?.signal });
      thinking.remove();
      renderResult(result);
    } catch (err) {
      thinking.remove();
      if (err?.name === 'AbortError') addRetry(addMsg('error', 'Stopped.'), text, send, attachments);
      else if (err instanceof LlmError && err.kind === 'truncated') addMsg('error', err.message);
      else if (err instanceof LlmError && err.kind === 'refusal') addMsg('error', `The model refused: ${err.message}`);
      else if (err instanceof LlmError && err.kind === 'disabled') addMsg('error', 'The assistant is not enabled on this server (no API key configured).');
      else {
        // A failed turn offers a one-click Retry: the SAME text re-sent through
        // the normal path (nothing is auto-retried; a Stop never offers one).
        // turnFailureText: the browser's error voice, same words on every surface.
        const el = addMsg('error', turnFailureText(llmSettings, err));
        // Icon-only (the composer buttons' shape) — a labelled button inside the
        // bubble reads as part of the message.
        addRetry(el, text, send, attachments);
      }
    } finally {
      turnAbort = null;
      setBusy(false);
      // §10 clearChat, confirmed: tear the conversation down through the existing
      // clear flow — this turn included — now that busy has dropped.
      if (wipeAfterTurn) {
        wipeAfterTurn = false;
        clearConversation();
      }
      inputEl.focus();
    }
  };

  // ── Lazy boot: wire handlers + settings the first time the section opens ──
  const boot = () => {
    if (booted) return;
    booted = true;

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
                              ['chat-open-options', 'gear']]) {
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

    // The "…" overflow: attach / clear / settings (browser + desktop parity).
    // The items reuse the existing handlers — only the affordance moved.
    {
      const moreBtn = document.getElementById('chat-more-btn');
      const moreMenu = document.getElementById('chat-more-menu');
      const attachInput = document.getElementById('chat-attach-input');
      const closeMore = () => {
        moreMenu.hidden = true;
        moreBtn.setAttribute('aria-expanded', 'false');
      };
      moreBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        moreMenu.hidden = !moreMenu.hidden;
        moreBtn.setAttribute('aria-expanded', String(!moreMenu.hidden));
      });
      for (const item of moreMenu.querySelectorAll('.chat-more-item'))
        item.addEventListener('click', closeMore);
      document.addEventListener('pointerdown', (e) => {
        if (!moreMenu.hidden && !moreMenu.contains(e.target) && e.target !== moreBtn) closeMore();
      });
      document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeMore(); });
      // Add image → the hidden picker; its files ride the same queue as a drop.
      document.getElementById('chat-attach-btn').addEventListener('click', () => {
        if (!busy) attachInput.click();
      });
      attachInput.addEventListener('change', async () => {
        const files = [...(attachInput.files || [])];
        attachInput.value = '';   // re-picking the same file must fire again
        if (files.length) await queueFiles(files);
      });
      // Settings → the extension's own Options page (the popup's gear path).
      document.getElementById('chat-open-options').addEventListener('click', () => {
        document.getElementById('open-options')?.click();
      });
    }

    // ── Per-message context menu: Copy / Insert into prompt / Resend ────────────
    // Right-click on a transcript bubble. Same .action-menu language as the popup's
    // row menu; Resend only on the user's own turns (their attachments requeue too).
    {
      const msgTargetOf = (el) => {
        const meta = msgMeta.get(el) || { text: el.textContent || '' };
        return {
          el,
          role: el.classList.contains('user') ? 'user' : 'assistant',
          text: meta.text,
          resendText: meta.resendText != null ? meta.resendText : meta.text,
          attachments: meta.attachments || [],
        };
      };
      const msgMenu = createMsgMenu({
        doc: document,
        renderIcon: (name) => icon(name, { size: 15 }),
        actions: {
          copy: (t) => { navigator.clipboard?.writeText(t.text)?.catch?.(() => {}); },
          insert: (t) => appendToPrompt(inputEl, t.text),
          resend: (t) => { if (!busy) send(t.resendText, t.attachments); },
        },
      });
      document.body.appendChild(msgMenu.el);
      // Both affordances — right-click and the hover "⋯" — open through here.
      const openMenuAt = (el, x, y) => msgMenu.openFor(msgTargetOf(el), {
        x, y, viewport: { width: window.innerWidth, height: window.innerHeight },
      });
      transcriptEl.addEventListener('contextmenu', (e) => {
        const el = e.target?.closest?.('.msg');
        if (!el || el.classList.contains('note') || el.classList.contains('typing-row')) return;
        if (el.closest('.disintegrate-host')) return;   // a leaving clone, not a live message
        // Only OUR default is suppressed; any selection the right-click landed on
        // stays as it is (the menu's own mousedown is inert — see chatMsgMenu.js).
        e.preventDefault();
        openMenuAt(el, e.clientX, e.clientY);
      });
      // The hover-revealed "⋯" each bubble carries (addMsg): the SAME menu, anchored
      // just under the button (clampMenuPosition pulls it back inside the viewport).
      addMsgMenuBtn = (el) => {
        const btn = createMsgMenuButton({
          doc: document, renderIcon: (n) => icon(n, { size: 13 }),
          role: el.classList.contains('user') ? 'user' : 'assistant',
        });
        btn.addEventListener('click', (e) => {
          e.stopPropagation();
          const r = btn.getBoundingClientRect();
          openMenuAt(el, r.left, r.bottom + 2);
        });
        el.appendChild(btn);
      };
      document.addEventListener('pointerdown', (e) => {
        if (msgMenu.isOpen() && !msgMenu.el.contains(e.target)) msgMenu.close();
      });
      // Escape dismissal lives in the menu itself (chatMsgMenu.js, open-scoped).
      transcriptEl.addEventListener('scroll', () => msgMenu.close());
    }

    // The COMPOSER is the drop target — not the whole section: over the transcript the
    // drop belongs to the page behind it (browser chatPanel.js parity). The cue is
    // built here once rather than in the three host pages.
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
      if (area === 'sync') settingsPromise = null;
      if (area === 'local' && changes[LLM_SETTINGS_KEY]) refreshSettings();
    });

    refreshSettings().then(() => {
      showSuggestions();   // the drag-hint note rides the empty state (chips included)
    });
  };

  return {
    // Wired into popup.js's generic section toggler for #sec-assistant.
    handleToggle(collapsed) { if (!collapsed) boot(); },

    // The ✦ header button: expand (through the section head, so aria-expanded and
    // the toggle hook stay in sync), scroll into view, focus the input.
    reveal() {
      if (sectionEl.classList.contains('collapsed')) sectionEl.querySelector('.section-head').click();
      sectionEl.scrollIntoView({ block: 'end' });
      inputEl.focus();
    },
  };
};
