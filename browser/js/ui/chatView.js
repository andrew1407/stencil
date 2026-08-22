// ── Shared assistant rendering (chat panel + context-menu chat) ─────────────
// Both surfaces show the SAME conversation (js/llm/chatSession.js) with the same DOM.
// Model output is DATA — every string lands via textContent, never innerHTML. The
// context menu scopes these with .ctx-assist; structure and affordances are identical.
import { icon } from './icons.js';
import { escapeHtml } from './base.js';
import { notify } from '../utils.js';
import { sanitizeLabel, askAnswerText } from '../llm/opPlan.js';
import {
  observeReveal, leaveThenRemove, LEAVING_CLASS, wipeDurationMs,
  CHAT_LEAVE_MS, CHIP_LEAVE_MS, scatterGridFor, menuPopOrigin,
  REVEAL_ITEM_CLASS, REVEAL_IN_CLASS, REVEAL_MASKED_CLASS, REVEAL_ENTERING_CLASS,
  REVEAL_SMOOTH_CLASS, REVEAL_NO_TRIGGER_CLASS, surfaceIn, surfaceOut,
} from './motion.js';

// Every chat entry leaves on the same dissolve. `count` is how many are going at once
// — one removal gets the full fine mesh, a whole-transcript wipe coarsens so the total
// number of flying tiles stays inside the budget (motion.js scatterGridFor).
const chatLeave = (el, done, count = 1, index = 0) =>
  leaveThenRemove(el, done, { ms: CHAT_LEAVE_MS, ...scatterGridFor(count, index) });
// Chips ride the longer chip clock (css chipLeave hold + collapse — see motion.js).
const chipLeave = (el, done) =>
  leaveThenRemove(el, done, { ms: CHIP_LEAVE_MS, ...scatterGridFor(1, 0) });

// ── The empty state: ONE set of suggestion chips for every chat surface ─────
// Clicking a chip prefills that surface's input (each wires a delegated listener on
// its transcript, which survives the re-renders below). The list lives here so the
// panel and the context-menu flyout can never drift apart.
export const CHAT_SUGGESTIONS = [
  { prompt: 'Make it sepia', label: 'Make it sepia' },
  { prompt: 'Give me 3 variants: rotated, tinted, cropped', label: '3 variants: rotated · tinted · cropped' },
  { prompt: 'Extract the lines from this image', label: 'Extract the lines from this image' },
  { prompt: 'Crop 10% off every edge and rotate right', label: 'Crop 10% off every edge, rotate right' },
];
// The chips as markup, for the two static templates (so the first paint already has
// them, before any JS runs).
export const chatSuggestionsHtml = () => CHAT_SUGGESTIONS
  .map((s) => `<button type="button" class="chat-suggest" data-prompt="${escapeHtml(s.prompt)}">${escapeHtml(s.label)}</button>`)
  .join('\n                ');
// Three bouncing dots for an in-flight turn — the same look in the extension panel and
// the desktop dock. Stops moving under prefers-reduced-motion (see animations.css).
export const typingDots = () => {
  const wrap = document.createElement('span');
  wrap.className = 'chat-typing';
  wrap.setAttribute('role', 'status');
  wrap.setAttribute('aria-label', 'Assistant is answering');
  for (let i = 0; i < 3; i++) wrap.appendChild(document.createElement('i'));
  return wrap;
};

// The cue shown over the COMPOSER while a drag hovers it: an animated icon beside the
// label. It lives in the composer (not over the whole panel) because that is exactly
// where a drop attaches — the transcript above belongs to the canvas's own drop.
export const CHAT_DROP_CUE = 'Drop to attach';
export const chatDropCueHtml = (id = 'chat-drop-cue') =>
  `<div class="chat-drop-cue" id="${id}" aria-hidden="true">`
  + `<span class="chat-drop-cue-icon">${icon('image', { size: 16 })}</span>`
  + `<span>${escapeHtml(CHAT_DROP_CUE)}</span></div>`;

export const chatEmptyState = () => {
  const wrap = document.createElement('div');
  wrap.className = 'chat-empty';
  for (const s of CHAT_SUGGESTIONS) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'chat-suggest';
    b.dataset.prompt = s.prompt;
    b.textContent = s.label;
    wrap.appendChild(b);
  }
  return wrap;
};

// The composer's action row, shared by the panel and the context-menu flyout (each
// surface keeps its own `${prefix}-…` ids). Only SEND stays inline; attach / clear /
// settings live behind the "…" trigger carrying the provider-status dot — the real
// controls stay in the DOM inside the menu, so existing ids and listeners keep working.
export const chatComposerActionsHtml = ({ prefix, actionsClass, gearClass, trailingHtml = '' }) => `<span class="${actionsClass}">
                <button id="${prefix}-send" disabled class="btn-icon ${prefix}-abtn" title="Send (Enter)">${icon('send', { size: 14 })}</button>
                <span class="chat-more-wrap">
                    <button id="${prefix}-more-btn" class="btn-icon ${prefix}-abtn ${gearClass}" aria-haspopup="true" aria-expanded="false" aria-label="More actions">${icon('dots', { size: 14 })}<span id="${prefix}-status-dot" class="conn-status conn-status-connecting"></span></button>
                    <span class="chat-more-menu" id="${prefix}-more-menu" hidden>
                        <button id="${prefix}-attach-btn" class="chat-more-item">${icon('image', { size: 14 })}<span>Add image</span></button>
                        <button id="${prefix}-clear" class="chat-more-item">${icon('trash', { size: 14 })}<span>Clear history</span></button>
                        <button id="${prefix}-settings-btn" class="chat-more-item" aria-label="Assistant settings — provider &amp; model">${icon('gear', { size: 14 })}<span>Settings</span></button>
                    </span>
                </span>
                <input type="file" id="${prefix}-attach-input" accept="image/*,video/*" multiple style="display:none;">${trailingHtml}
            </span>`;

// Wire one composer's "…" menu: toggle on click, close on outside click, on
// Escape, and after any item runs (the item's own listener still does the work).
export const wireChatMoreMenu = (prefix, doc = document, { onOpen } = {}) => {
  const btn = doc.getElementById(`${prefix}-more-btn`);
  const menu = doc.getElementById(`${prefix}-more-menu`);
  if (!btn || !menu) return;
  // Both edges go through setOpen, so the popup accounting (and the pills that stand
  // down for it) can never disagree with what is on screen.
  const setOpen = (on) => {
    menu.hidden = !on;
    btn.setAttribute('aria-expanded', String(on));
    if (on) openComposerMenus.add(menu); else openComposerMenus.delete(menu);
    announcePopup();
  };
  const close = () => setOpen(false);
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    if (menu.hidden) onOpen?.();   // refresh item enabled-states as the menu appears
    setOpen(menu.hidden);
  });
  for (const item of menu.querySelectorAll('.chat-more-item')) item.addEventListener('click', close);
  doc.addEventListener('pointerdown', (e) => { if (!menu.hidden && !menu.contains(e.target) && e.target !== btn) close(); });
  doc.addEventListener('keydown', (e) => { if (e.key === 'Escape' && !menu.hidden) close(); });
};

// The send ↔ Stop swap both composers share: while a turn is in flight the send
// button becomes Stop (enabled, stop glyph) and attaching pauses; otherwise send
// is enabled only when there is text.
export const syncComposerControls = ({ sendBtn, attachBtn, input }, sending, { attachFull = false } = {}) => {
  sendBtn.disabled = sending ? false : !input.value.trim();
  sendBtn.innerHTML = icon(sending ? 'stop' : 'send', { size: 14 });
  sendBtn.title = sending ? 'Stop the response' : 'Send (Enter)';
  attachBtn.disabled = sending || attachFull;   // full = MAX_ATTACHMENTS already queued
};

// Wire one composer: Enter sends / Shift+Enter newline, send doubles as Stop, and
// the attach button drives the hidden file input. Hooks keep each surface's deltas:
//   isSending()      turn in flight?
//   abort()          the Stop action
//   submit(text)     run the (already-dequeued) turn — surface owns control sync
//   attachFiles(fs)  queue picked Files on the shared controller
//   onInput()        control sync (and any surface extras) on typing
export const wireChatComposer = ({ input, sendBtn, attachBtn, attachInput }, { isSending, abort, submit, attachFiles, onInput }) => {
  const send = () => {
    const text = input.value.trim();
    if (!text || isSending()) return;
    input.value = '';
    submit(text);
  };
  input.addEventListener('input', () => onInput?.());
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); send(); }
  });
  sendBtn.addEventListener('click', () => { if (isSending()) abort(); else send(); });
  attachBtn.addEventListener('click', () => attachInput.click());
  attachInput.addEventListener('change', async (e) => {
    const files = [...(e.target.files || [])];
    e.target.value = '';
    await attachFiles(files);
  });
};

// Render the SHARED transcript log (js/llm/chatSession.js) into `transcript`.
// Reset a row's classes without dropping the motion classes motion.js owns — repaints
// rewrite className wholesale, which would strand a revealed row at its dimmed rest state.
const MOTION_CLASSES = [REVEAL_ITEM_CLASS, REVEAL_IN_CLASS, REVEAL_MASKED_CLASS, REVEAL_ENTERING_CLASS,
  REVEAL_SMOOTH_CLASS, REVEAL_NO_TRIGGER_CLASS];
const setRowClass = (el, cls) => {
  const keep = MOTION_CLASSES.filter((k) => el.classList.contains(k));
  el.className = cls;
  if (keep.length) el.classList.add(...keep);
};

// A row's text lives in ONE dedicated child, never as loose text beside the buttons
// the row also carries — so "a bubble shows its text exactly once" is structural.
const rowTextNode = (el) => {
  let t = el.querySelector('.chat-msg-text');
  if (!t) {
    t = document.createElement('div');
    t.className = 'chat-msg-text';
    el.prepend(t);
  }
  return t;
};

// Keyed by row id and incremental: existing rows update in place, new rows append in
// log order, gone rows are removed. Both surfaces call this on every log change —
// that keeps them in lockstep and renders history a surface opened late has missed.
export const renderChatLog = (transcript, log, { onConfigure, onAskSubmit, onRetry, onReconnect } = {}) => {
  // Chat stickiness, measured BEFORE the mutations below: follow the conversation
  // only when the user is already at (or near) the bottom — never yank them out of
  // history they scrolled up to read.
  const stick = transcript.scrollHeight - transcript.clientHeight - transcript.scrollTop < 40;
  // ONE rule, one place: an empty conversation shows the suggestion chips, anything
  // else doesn't. Bringing them BACK waits until the rows finish leaving (see
  // restoreEmptyState) — chips reappearing over still-scattering rows flicker.
  if (log.length) transcript.querySelector('.chat-empty')?.remove();
  // Scroll reveal, bound here rather than per surface so the panel and the
  // context-menu flyout both get it. Idempotent — one observer per transcript.
  // smooth: these rows are TEXT — a grainy dissolve on the cut edge reads as corruption.
  if (!transcript._revealBound) transcript._revealBound = observeReveal(transcript, '[data-row]', { smooth: true });
  const live = new Set();
  for (const row of log) {
    live.add(String(row.id));
    let el = transcript.querySelector(`[data-row="${row.id}"]`);
    if (!el) {
      el = document.createElement('div');
      el.dataset.row = row.id;
      transcript.appendChild(el);
    }
    // A row that will carry a Retry becomes a flex COLUMN for it. Part of the class
    // string, not a later classList.add: setRowClass rewrites className wholesale on
    // every repaint, so anything added afterwards is lost on the next log change.
    const retryable = !!(row.error && row.retryText && onRetry);
    setRowClass(el, `chat-msg chat-msg-${row.role}`
      + (row.error ? ' chat-msg-error' : '')
      + (row.card ? ' chat-error-card' : '')
      + (retryable ? ' chat-msg-cta' : ''));
    // The row menu (wireChatRowMenu) reads the CURRENT log row off its element.
    el._chatRow = row;
    // An in-flight turn shows bouncing dots; everything else is model output as DATA.
    // Both land in the row's ONE text node, only when actually changed: a rewrite per
    // repaint would tear a held selection out of the user's hands.
    const textEl = rowTextNode(el);
    const typing = textEl.querySelector('.chat-typing');
    if (row.pending) {
      if (!typing) { textEl.textContent = ''; textEl.appendChild(typingDots()); }
      // A SETTLED row must lose the dots even when its text did not "change": a pending
      // bubble reads as '', so a turn answering with '' left the dots spinning forever.
    } else if (typing || textEl.textContent !== row.text) textEl.textContent = row.text;
    // The unreachable-provider card adds its configure CTA beside that text — built
    // once, like every other affordance on the row. An EXPIRED session takes the
    // reconnect CTA instead: the provider is configured correctly, the token is dead.
    const cta = row.reconnect ? '.chat-reconnect-cta' : '.chat-config-cta';
    if (row.card && !el.querySelector(cta)) {
      el.appendChild(row.reconnect
        ? chatReconnectButton(row.reconnect, onReconnect)
        : chatConfigureButton(onConfigure));
    }
    if (!row.card || row.reconnect) el.querySelector('.chat-config-cta')?.remove();
    if (!row.card || !row.reconnect) el.querySelector('.chat-reconnect-cta')?.remove();
    // A failed turn that remembers its prompt offers a one-click Retry — the same text
    // through the normal path (nothing is auto-retried). Built once: the row's buttons
    // survive a repaint, so re-appending here would stack a second button.
    if (retryable && !el.querySelector('.chat-retry-cta')) {
      // Icon-only (the header ghosts' shape): the row is a text bubble, so a
      // labelled button reads as part of the message.
      const retry = document.createElement('button');
      retry.className = 'chat-hbtn chat-retry-cta';
      retry.title = 'Send this message again';
      retry.setAttribute('aria-label', 'Retry');
      retry.innerHTML = icon('refresh', { size: 13 });
      retry.addEventListener('click', () => onRetry(row.retryText));
      el.appendChild(retry);
    }
    // Hover "…" trigger (CSS reveals it on hover-capable pointers only), built once:
    // the text now lives in its own node, so a rewrite no longer wipes the row's
    // buttons. Every settled row gets one — the first/oldest included.
    if (!el.querySelector('.chat-row-menu-btn')) {
      const more = chatRowMenuButton(row);
      if (more) el.appendChild(more);
    }
    // What the user attached rides in its own row just BEFORE their message, on the
    // user's side — the images are part of what they said. Built once (like the
    // result cards): a repaint must not reload the thumbnails.
    const attachId = `${row.id}-attachments`;
    if (row.attachments && row.attachments.length) {
      live.add(attachId);
      if (!transcript.querySelector(`[data-row="${attachId}"]`)) {
        const strip = chatAttachmentStrip(row.attachments);
        strip.dataset.row = attachId;
        el.before(strip);
      }
    }
    // Result cards ride in their own row right after the message they belong to.
    const resultsId = `${row.id}-results`;
    let cards = transcript.querySelector(`[data-row="${resultsId}"]`);
    if (row.results && row.results.length) {
      live.add(resultsId);
      if (!cards) {
        cards = document.createElement('div');
        cards.dataset.row = resultsId;
        cards.className = 'chat-results';
        for (const r of row.results) cards.appendChild(chatResultCard(r));
        el.after(cards);
      }
    } else if (cards && !cards.classList.contains(LEAVING_CLASS)) {
      cards.removeAttribute('data-row');   // out of every lookup the moment it starts leaving
      chatLeave(cards, () => cards.remove());
    }
    // …and the §11 choice card rides after those, in its own row. Built ONCE: rebuilding it
    // on every repaint would wipe a half-made selection (and re-fire an answered card).
    const askId = `${row.id}-ask`;
    let askEl = transcript.querySelector(`[data-row="${askId}"]`);
    if (row.ask) {
      live.add(askId);
      if (!askEl) {
        askEl = chatAskCard(row.ask, {
          previews: row.askPreviews || [],
          onSubmit: (answer) => onAskSubmit?.(answer, row),
        });
        askEl.dataset.row = askId;
        (cards || el).after(askEl);
      }
    } else if (askEl && !askEl.classList.contains(LEAVING_CLASS)) {
      askEl.removeAttribute('data-row');
      chatLeave(askEl, () => askEl.remove());
    }
  }
  // Rows the log dropped (Clear, or a card that answered) dissolve instead of
  // blinking out. A row already on its way out is skipped — this runs on EVERY log
  // change, and re-arming it would restart the animation and never finish.
  const going = [...transcript.querySelectorAll('[data-row]')].filter((el) => !live.has(el.dataset.row));
  const wiped = going.length > 0;
  going.forEach((el, i) => {
    el.removeAttribute('data-row');   // gone from every lookup, so no repaint re-finds it
    chatLeave(el, () => el.remove(), going.length, i);
  });
  restoreEmptyState(transcript, log, wiped);
  if (stick) stickToBottom(transcript);
};

// Pin the transcript to its bottom NOW and again as the entrance animations settle:
// a freshly appended row grows AFTER the first measure, so a single scrollTop write
// landed one row short of the indicator.
export const stickToBottom = (transcript) => {
  const pin = () => { transcript.scrollTop = transcript.scrollHeight; };
  pin();
  if (typeof requestAnimationFrame === 'function') requestAnimationFrame(pin);
  setTimeout(pin, 220);   // reveal/entrance settle
};

// Bring the empty state back only once the transcript has actually emptied ON SCREEN:
// rows go first, then the chips (the order core/storage.js uses for the cleared canvas).
// Idempotent under repaints: one waiter, re-checking the world before painting.
const restoreEmptyState = (transcript, log, wiped) => {
  const paint = () => {
    transcript._emptyWaiting = false;
    // The conversation may have restarted (or the surface been torn down) while the
    // particles fell — in either case the chips are no longer the right answer.
    if (log.length || transcript.querySelector('[data-row]')) return;
    if (!transcript.querySelector('.chat-empty')) transcript.prepend(chatEmptyState());
  };
  if (log.length) return;
  const leaving = wiped || transcript.querySelector(`.${LEAVING_CLASS}`);
  if (!leaving) { paint(); return; }
  if (transcript._emptyWaiting) return;
  transcript._emptyWaiting = true;
  setTimeout(paint, wipeDurationMs());
};

// Wire the suggestion chips ONCE, on the transcript itself: the .chat-empty block is
// replaced whenever the conversation empties, so a listener bound to it would die with it.
export const wireChatSuggestions = (transcript, onPick) => {
  transcript.addEventListener('click', (e) => {
    const chip = e.target.closest('.chat-suggest');
    if (chip && transcript.contains(chip)) onPick(chip.dataset.prompt);
  });
};

// Window-level pointer tracking for a drag gesture: move handler + one-shot up/cancel.
export const trackPointer = (onMove, onUp) => {
  const up = (ev) => {
    window.removeEventListener('pointermove', onMove);
    window.removeEventListener('pointerup', up);
    window.removeEventListener('pointercancel', up);
    onUp(ev);
  };
  window.addEventListener('pointermove', onMove);
  window.addEventListener('pointerup', up);
  window.addEventListener('pointercancel', up);
};

// The slider-style strip ABOVE a composer resizes the textarea (the native grip is
// off — the input is bottom-anchored, only its top edge can move). `onDrag` lets the
// flyout re-place itself; `hold` keeps the gesture from reading as "the pointer left".
export const wireInputSizer = (sizer, input, { host, onDrag, hold } = {}) => {
  sizer.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    const startY = e.clientY;
    const startH = input.getBoundingClientRect().height;
    try { sizer.setPointerCapture(e.pointerId); } catch { /* capture is best-effort */ }
    sizer.classList.add('dragging');
    host?.classList.add('chat-gesturing');
    hold?.(true);
    trackPointer((ev) => {
      input.style.height = Math.round(startH + (startY - ev.clientY)) + 'px';
      onDrag?.();
    }, () => {
      sizer.classList.remove('dragging');
      host?.classList.remove('chat-gesturing');
      hold?.(false);
      onDrag?.();
    });
  });
};

// The images the user attached to one turn, as a thumbnail strip above their message.
// `name` is a filename — untrusted, so it rides `alt`/`title` as data, never markup. A
// video shows its first sampled frame — frames are what the model sees (contract §7).
// The hover preview is fixed-position on the BODY: the panel clips its own overflow
// (and the extension popup is 400px wide), so an in-place popup would be cut off.
export const THUMB_PREVIEW_MAX = 320;
let thumbPreviewEl = null;
export const hideThumbPreview = () => { thumbPreviewEl?.remove(); thumbPreviewEl = null; };
// Switching window never fires the thumbnail's mouseleave — hide on blur, module-wide.
// Alt HELD doubles the glance (chat-thumb-preview-xl); pressed or released mid-hover
// it resizes in place and the open preview re-places itself to stay in view.
if (typeof window !== 'undefined') {
  window.addEventListener('blur', hideThumbPreview);
  const altPreview = (on) => {
    if (!thumbPreviewEl) return;
    thumbPreviewEl.classList.toggle('chat-thumb-preview-xl', on);
    thumbPreviewEl.__place?.();
  };
  window.addEventListener('keydown', (e) => { if (e.key === 'Alt') altPreview(true); });
  window.addEventListener('keyup', (e) => { if (e.key === 'Alt') altPreview(false); });
}
export const wireThumbPreview = (img, caption = '') => {
  const place = (box) => {
    const r = img.getBoundingClientRect();
    const b = box.getBoundingClientRect();
    const left = Math.max(8, Math.min(r.left, window.innerWidth - b.width - 8));
    // Above the thumbnail by default; below when there isn't room up there.
    const above = r.top - b.height - 10;
    const top = above >= 8 ? above : Math.min(r.bottom + 10, window.innerHeight - b.height - 8);
    box.style.left = `${Math.round(left)}px`;
    box.style.top = `${Math.round(Math.max(8, top))}px`;
    box.classList.add('chat-thumb-preview-in');
  };
  img.addEventListener('mouseenter', (e) => {
    hideThumbPreview();
    const box = document.createElement('div');
    box.className = 'chat-thumb-preview';
    if (e.altKey) box.classList.add('chat-thumb-preview-xl');   // Alt already held on entry
    const big = document.createElement('img');
    big.src = img.src;
    big.alt = '';
    box.appendChild(big);
    if (caption) {
      const cap = document.createElement('span');
      cap.className = 'chat-thumb-preview-cap';
      cap.textContent = caption;   // a filename — data, like everywhere else
      box.appendChild(cap);
    }
    document.body.appendChild(box);
    thumbPreviewEl = box;
    box.__place = () => place(box);   // the Alt resize re-places the open box
    // Measure only once the picture has its size, or the flip decides on an empty box.
    if (big.complete) place(box); else big.addEventListener('load', () => place(box), { once: true });
  });
  img.addEventListener('mouseleave', hideThumbPreview);
  // A scroll (or a click anywhere) moves the anchor out from under the preview.
  img.addEventListener('click', hideThumbPreview);
  window.addEventListener('scroll', hideThumbPreview, { capture: true });
};

export const chatAttachmentStrip = (attachments) => {
  const strip = document.createElement('div');
  strip.className = 'chat-attached';
  for (const a of attachments) {
    const fig = document.createElement('span');
    fig.className = 'chat-attached-item';
    // No native title here: the hover preview below already shows the name WITH the
    // big image, and the two tooltips doubled up.
    const img = document.createElement('img');
    img.className = 'chat-attached-thumb';
    img.src = a.dataUrl;
    img.alt = a.name;
    // Small on purpose — hovering shows it big (the thumbnail alone is too small to
    // tell two screenshots apart).
    wireThumbPreview(img, a.kind === 'video' ? `${a.name} (first frame)` : a.name);
    fig.appendChild(img);
    if (a.kind === 'video') {
      const badge = document.createElement('span');
      badge.className = 'chat-attached-badge';
      badge.textContent = 'video';
      fig.appendChild(badge);
    }
    strip.appendChild(fig);
  }
  return strip;
};

// One result card: thumbnail + label + download + open-as-the-working-image.
export const chatResultCard = (r) => {
  const card = document.createElement('div');
  card.className = 'chat-result';
  const img = document.createElement('img');
  img.className = 'chat-result-thumb';
  img.src = r.dataUrl;
  img.alt = r.label;
  img.title = r.label;
  const label = document.createElement('span');
  label.className = 'chat-result-label';
  label.textContent = r.label;
  const dl = document.createElement('a');
  dl.className = 'chat-hbtn chat-result-btn';
  dl.title = `Download ${r.label}`;
  dl.download = `${sanitizeLabel(r.label)}.png`;
  dl.href = r.dataUrl;
  dl.innerHTML = icon('download', { size: 13 });
  const use = document.createElement('button');
  use.className = 'chat-hbtn chat-result-btn';
  use.title = `Open ${r.label} as the working image`;
  use.innerHTML = icon('external', { size: 13 });
  use.addEventListener('click', async () => {
    try { await window.stencil.load(r.dataUrl, { name: `${sanitizeLabel(r.label)}.png` }); }
    catch (err) { notify(`Could not open ${r.label} — ${err.message}`, 'fail'); }
  });
  card.append(img, label, dl, use);
  return card;
};

// ── §11 choice card ─────────────────────────────────────────────────────────
// The model's `ask` rendered under its reply: radios/checkboxes per mode, per-option
// previews, optional free-text, Submit disabled until something is chosen. Submitting
// sends the answer as the user's NEXT turn (§11.3); nothing here applies an edit. Every
// string is model output → textContent. Once answered the card locks — no re-firing.
export const chatAskCard = (ask, { onSubmit, previews = [] } = {}) => {
  const byIndex = new Map(previews.map((p) => [p.index, p.dataUrl]));
  const wrap = document.createElement('div');
  wrap.className = 'chat-ask';

  const q = document.createElement('div');
  q.className = 'chat-ask-q';
  q.textContent = ask.question;
  wrap.appendChild(q);

  const name = `ask-${Math.random().toString(36).slice(2)}`;   // groups the radios
  const multi = ask.mode === 'multi';
  const list = document.createElement('div');
  list.className = 'chat-ask-options';
  const inputs = [];
  ask.options.forEach((opt, i) => {
    const row = document.createElement('label');
    row.className = 'chat-ask-option';
    const box = document.createElement('input');
    box.type = multi ? 'checkbox' : 'radio';
    box.name = name;
    box.value = String(i);
    inputs.push(box);
    row.appendChild(box);
    // ONLY from `previews` — data: URLs this app rendered. An option's model-written
    // `image.url` must never reach an <img src>: that fires a request to a host the
    // MODEL chose, on render, before the user has read the card.
    const pic = byIndex.get(i);
    if (pic) {
      const img = document.createElement('img');
      img.className = 'chat-ask-thumb';
      img.src = pic;
      img.alt = '';
      row.appendChild(img);
    }
    const label = document.createElement('span');
    label.className = 'chat-ask-label';
    label.textContent = opt.label;
    row.appendChild(label);
    list.appendChild(row);
  });
  wrap.appendChild(list);

  // The custom row: picking it is what makes its input meaningful, and typing in it picks
  // it — so the two can't disagree about what will be sent.
  let customBox = null;
  let customText = null;
  if (ask.allowCustom) {
    const row = document.createElement('label');
    row.className = 'chat-ask-option chat-ask-custom';
    customBox = document.createElement('input');
    customBox.type = multi ? 'checkbox' : 'radio';
    customBox.name = name;
    customBox.value = 'custom';
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
    // Removing the button below detaches it but does NOT disarm this listener — a retained
    // reference (or assistive tech) could click it again and re-send the turn. The flag is
    // what makes "answered once" true, not the DOM.
    if (answered) return;
    const answer = askAnswerText(ask, { picked: chosen(), custom: typed() });
    if (!answer) return;
    answered = true;
    // Lock it: the card becomes a record of what was sent, not a control.
    for (const b of inputs) b.disabled = true;
    if (customText) customText.disabled = true;
    submit.remove();
    const sent = document.createElement('div');
    sent.className = 'chat-ask-sent';
    sent.textContent = answer;                    // the user's own words / picked labels
    actions.appendChild(sent);
    wrap.classList.add('chat-ask-answered');
    onSubmit?.(answer);
  });
  return wrap;
};

// Both chat surfaces read the SAME controller, so a queue change on one must repaint
// the other: whoever mutates attachments fires this. The name lives with the
// controller (chatController.js), which also fires it when a send drains the queue.
import { CHAT_ATTACHMENTS_EVENT } from '../llm/chatController.js';
export { CHAT_ATTACHMENTS_EVENT };
export const notifyAttachmentsChanged = () => window.dispatchEvent(new Event(CHAT_ATTACHMENTS_EVENT));

// The pending-attachment chips, shared by the panel and the context-menu composer;
// `controller` may be null (nothing queued — the row hides). Chips are keyed by the
// ATTACHMENT OBJECT and the row is patched in place, never rebuilt: a wholesale
// `innerHTML = ''` deleted the disintegrate layer mid-flight and replayed the
// survivors' entrances. PER CONTAINER: one chip node can only live in one list.
const CHIPS_BY_CONTAINER = new WeakMap();
export const chatAttachmentChips = (container, controller) => {
  let CHIP_FOR = CHIPS_BY_CONTAINER.get(container);
  if (!CHIP_FOR) { CHIP_FOR = new WeakMap(); CHIPS_BY_CONTAINER.set(container, CHIP_FOR); }
  const list = controller ? controller.attachments : [];
  for (let i = 0; i < list.length; i++) {
    const at = list[i];
    let chip = CHIP_FOR.get(at);
    // Already on screen: leave it exactly where it is — re-inserting a live node
    // RESTARTS its CSS animations. The queue only appends, so the order is right.
    if (chip && chip.isConnected && !chip.classList.contains(LEAVING_CLASS)) continue;
    chip = document.createElement('span');
    chip.className = 'chat-attach-chip';
    CHIP_FOR.set(at, chip);
    // The queued picture itself, small — hovering magnifies it (wireThumbPreview).
    // A name alone said nothing about WHICH image was queued, and a data: URL's name
    // is a wall of base64 (see fileNameForUrl) that reads as garbage in the chip.
    const label = at.kind === 'video' ? `${at.name} (${(at.frames || []).length} frames)` : at.name;
    const src = at.dataUrl || at.frames?.[0] || '';
    if (src) {
      const thumb = document.createElement('img');
      thumb.className = 'chat-attach-thumb';
      thumb.src = src;
      thumb.alt = at.name;
      wireThumbPreview(thumb, label);
      chip.appendChild(thumb);
    }
    const name = document.createElement('span');
    name.className = 'chat-attach-name';
    name.textContent = label;
    // The name is ellipsised in CSS, so the full one lives on the tooltip.
    name.title = label;
    const rm = document.createElement('button');
    rm.className = 'chat-hbtn chat-attach-remove';
    rm.setAttribute('aria-label', 'Remove attachment');   // no tooltip — the × says it
    rm.innerHTML = icon('x', { size: 12 });
    // The chip scatters before the queue drops it — and the row is patched, not
    // rebuilt, so the dust keeps flying over the chips that stay.
    rm.addEventListener('click', () => chipLeave(chip, () => {
      const at2 = controller.attachments.indexOf(at);
      if (at2 >= 0) controller.removeAttachment(at2);
      chip.remove();
      notifyAttachmentsChanged();
    }));
    chip.append(name, rm);
    container.appendChild(chip);
  }
  // Anything whose attachment is gone leaves the same way (unless it is already on
  // its way out, or it is the disintegrate layer, which owns its own lifetime).
  for (const el of [...container.children]) {
    if (el.classList.contains('disintegrate-host') || el.classList.contains(LEAVING_CLASS)) continue;
    const still = list.some((at) => CHIP_FOR.get(at) === el);
    if (!still) chipLeave(el, () => el.remove());
  }
  // The dust layer lives IN this container (motion.js appends to the parent), so an
  // empty queue must not hide the row while particles are still flying. Hide only
  // once nothing is animating; if something is, check back after the wipe.
  const animating = () => [...container.children].some((el) =>
    el.classList.contains('disintegrate-host') || el.classList.contains(LEAVING_CLASS));
  if (list.length || animating()) {
    container.style.display = '';
    if (!list.length) {
      setTimeout(() => {
        if (!(controller?.attachments?.length) && !animating()) container.style.display = 'none';
      }, wipeDurationMs() + 50);
    }
  } else {
    container.style.display = 'none';
  }
};

// ── Right-click menu on transcript rows ─────────────────────────────────────
// One floating menu for any settled row, shared by the panel and the context-menu
// flyout, styled as the projects modal's row menu (components.css aliases
// .chat-row-menu). renderChatLog stamps each row element with its log row
// (el._chatRow, refreshed per repaint), so the menu always reads the CURRENT row.
// The items, data-driven and pure: every settled row gets Copy / Insert into
// prompt; USER rows add Resend (same text + original attachments).
export const chatRowMenuItems = (row) => {
  if (!row || row.pending) return [];
  const items = [
    { id: 'copy', label: 'Copy message', icon: 'copy' },
    { id: 'insert', label: 'Insert into prompt', icon: 'pencil' },
  ];
  if (row.role === 'user') items.push({ id: 'resend', label: 'Resend', icon: 'send' });
  return items;
};

// The hover "…" trigger a settled row wears (renderChatLog appends it): opens the
// same menu as right-click. It sits on the bubble corner facing the panel centre —
// user bubbles are right-aligned so bottom-LEFT, assistant ones bottom-RIGHT.
export const chatRowMenuButton = (row) => {
  if (!chatRowMenuItems(row).length) return null;
  const b = document.createElement('button');
  b.type = 'button';
  b.className = `chat-row-menu-btn chat-row-menu-btn-${row.role === 'user' ? 'left' : 'right'}`;
  // A real control, so labelled — not aria-hidden (a click focuses it and Chrome
  // rejects hiding a focused element). mousedown is inert to keep text selections.
  b.setAttribute('aria-label', 'Message actions');
  b.tabIndex = -1;
  b.innerHTML = icon('more', { size: 13 });
  b.addEventListener('mousedown', (e) => e.preventDefault());
  return b;
};

// The jump pills float exactly where a cut row parks its "…" and sit at a higher
// z-index, so they swallowed the trigger. Desktop parity: the pills stand down while
// the trigger would touch them. Pure rect test — `pad` keeps them from sitting flush.
export const rowMenuHitsJumps = (btn, pills = [], pad = 4) => {
  if (!btn || !(btn.width > 0)) return false;
  return pills.some((p) => p && p.width > 0 && p.height > 0
    && btn.left < p.right + pad && btn.right > p.left - pad
    && btn.top < p.bottom + pad && btn.bottom > p.top - pad);
};

// Copy `text` to the clipboard: the async API first, the hidden-textarea
// execCommand fallback where it is missing or refused. Resolves true on success —
// the caller owns the failure toast.
export const copyChatText = async (text, doc = document,
  nav = typeof navigator === 'undefined' ? null : navigator) => {
  try {
    if (nav?.clipboard?.writeText) { await nav.clipboard.writeText(text); return true; }
  } catch { /* fall through to execCommand */ }
  try {
    const ta = doc.createElement('textarea');
    ta.value = text;
    ta.setAttribute('readonly', '');
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    doc.body.appendChild(ta);
    ta.select();
    const ok = !!doc.execCommand?.('copy');
    ta.remove();
    return ok;
  } catch { return false; }
};

// A right-click on text the user ALREADY selected in this row keeps the NATIVE
// menu — its Copy acts on exactly that selection, which the custom menu can't.
const selectionCoversRow = (rowEl, win = typeof window === 'undefined' ? null : window) => {
  const sel = win?.getSelection?.();
  if (!sel || sel.isCollapsed || !String(sel).trim()) return false;
  try { return sel.containsNode(rowEl, true); } catch { return false; }
};

// ONE open menu app-wide (module state, like the thumb preview): opening from the
// other surface, or re-opening on another row, replaces it.
let rowMenuEl = null;
let rowMenuClose = null;
export const chatRowMenuOpen = () => !!rowMenuEl;
export const closeChatRowMenu = () => { rowMenuClose?.(); };
// ── Any chat popup, and the one event that announces it ─────────────────────
// The jump pills stand down while ANY chat popup is up. One event on both edges; a
// listener re-reads chatPopupOpen() rather than tracking its own state, so nothing can
// latch. Covers the row menu (body-level) and every composer "…" menu (in-panel).
export const CHAT_POPUP_EVENT = 'stencil:chat-popup';
const openComposerMenus = new Set();
export const chatPopupOpen = () => !!rowMenuEl || openComposerMenus.size > 0;
const announcePopup = () => {
  try { window.dispatchEvent(new Event(CHAT_POPUP_EVENT)); } catch { /* no DOM */ }
};

const openChatRowMenu = (row, x, y, hooks) => {
  closeChatRowMenu();
  const menu = document.createElement('div');
  menu.className = 'chat-row-menu';
  // Must not bubble to the ctx-menu's document mousedown closer — clicking an
  // item here is chat use, not a click "outside the menu".
  menu.addEventListener('mousedown', (e) => e.stopPropagation());
  const close = () => {
    if (rowMenuEl !== menu) return;
    rowMenuEl = null;
    rowMenuClose = null;
    // Back into the point it grew out of (js/ui/motion.js). Its own layer, so the node
    // still goes NOW — the menu is never left half-removed for the sake of an effect.
    surfaceOut(menu, { x, y });
    menu.remove();
    announcePopup();   // …and the chrome that stood down comes back
    document.removeEventListener('pointerdown', onDown, true);
    document.removeEventListener('keydown', onKey, true);
    window.removeEventListener('scroll', close, true);
    window.removeEventListener('blur', close);
  };
  const onDown = (e) => { if (!menu.contains(e.target)) close(); };
  const onKey = (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(); } };
  const act = {
    copy: async () => {
      if (!(await copyChatText(row.text))) notify('Could not copy the message', 'fail');
    },
    insert: () => hooks.onInsert?.(row.text),
    resend: () => hooks.onResend?.(row.text, row.attachments || []),
  };
  for (const it of chatRowMenuItems(row)) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'chat-row-menu-item';
    b.innerHTML = icon(it.icon, { size: 15 });
    const label = document.createElement('span');
    label.textContent = it.label;
    b.appendChild(label);
    b.addEventListener('click', (e) => { e.stopPropagation(); close(); act[it.id](); });
    menu.appendChild(b);
  }
  document.body.appendChild(menu);
  // Cursor-anchored like the projects row menu: flip left/up near the edges.
  const mw = menu.offsetWidth;
  const mh = menu.offsetHeight;
  const left = Math.max(8, x + mw > window.innerWidth - 8 ? x - mw : x);
  const top = Math.max(8, y + mh > window.innerHeight - 8 ? y - mh : y);
  menu.style.left = `${left}px`;
  menu.style.top = `${top}px`;
  // The entry pop (animations.css menuPop) grows out of the open point — as dust when
  // motion.js can play it, and the plain pop is the fallback it leaves behind.
  menu.style.transformOrigin = menuPopOrigin(x, y, { left, top, width: mw, height: mh });
  surfaceIn(menu, { x, y });
  rowMenuEl = menu;
  rowMenuClose = close;
  announcePopup();
  // Wired a tick late, or the opening right-click's own events would close it.
  setTimeout(() => {
    if (rowMenuEl !== menu) return;
    document.addEventListener('pointerdown', onDown, true);
    document.addEventListener('keydown', onKey, true);
    // ANY scroll (the transcript's included) moves the anchor row out from under it.
    window.addEventListener('scroll', close, true);
    window.addEventListener('blur', close);
  }, 0);
};

// Touch has no hover, so no "…" trigger — instead a LONG-PRESS or a DOUBLE-TAP on a
// bubble opens the menu. Pure timing + distance recognizer (timers injectable) so the
// thresholds are unit-testable; wireChatRowMenu feeds it the raw touch events.
export const touchMenuGesture = (onOpen, {
  longPressMs = 500, moveTol = 10, doubleTapMs = 350,
  setTimer = (fn, ms) => setTimeout(fn, ms), clearTimer = (t) => clearTimeout(t),
} = {}) => {
  let timer = null, sx = 0, sy = 0, key = null, moved = false, fired = false;
  let lastTapKey = null, lastTapAt = -Infinity;
  const stop = () => { if (timer != null) { clearTimer(timer); timer = null; } };
  return {
    // Finger down on a row: arm the long-press at the touch point.
    start(k, x, y) {
      stop();
      key = k; sx = x; sy = y; moved = false; fired = false;
      timer = setTimer(() => { timer = null; fired = true; lastTapKey = null; onOpen(sx, sy); }, longPressMs);
    },
    // Drifting past the tolerance is a scroll, not a press — both gestures die.
    move(x, y) {
      if (moved || Math.hypot(x - sx, y - sy) <= moveTol) return;
      moved = true;
      stop();
    },
    // Lift. Returns true when THIS gesture opened the menu (long-press already
    // fired, or this tap completed a double-tap) — the caller suppresses the
    // native callout exactly then, never for ordinary taps.
    end(now = Date.now()) {
      const tap = timer != null && !moved;
      stop();
      if (!tap) { const opened = fired; fired = false; if (!opened) lastTapKey = null; return opened; }
      if (lastTapKey === key && now - lastTapAt <= doubleTapMs) {
        lastTapKey = null;
        onOpen(sx, sy);
        return true;
      }
      lastTapKey = key; lastTapAt = now;   // first tap: wait for a partner
      return false;
    },
    cancel() { stop(); moved = true; fired = false; lastTapKey = null; },
  };
};

// Wire one transcript: right-click on a settled .chat-msg row opens the menu at
// the pointer; the hover "…" trigger and the touch gestures open the same one.
// Hooks carry the surface's deltas (its composer, its send path):
//   onInsert(text)              append into this surface's composer and focus it
//   onResend(text, attachments) re-send a user turn with its original attachments
export const wireChatRowMenu = (transcript, hooks = {}) => {
  transcript.addEventListener('contextmenu', (e) => {
    const rowEl = e.target?.closest?.('.chat-msg');
    if (!rowEl || !transcript.contains(rowEl)) return;
    const row = rowEl._chatRow;
    if (!row || row.pending) return;
    if (selectionCoversRow(rowEl)) return;   // the native menu copies the selection
    e.preventDefault();
    e.stopPropagation();
    openChatRowMenu(row, e.clientX ?? 0, e.clientY ?? 0, hooks);
  });
  // The hover "…" trigger (renderChatLog appends it): same menu, anchored at the button.
  transcript.addEventListener('click', (e) => {
    const btn = e.target?.closest?.('.chat-row-menu-btn');
    if (!btn || !transcript.contains(btn)) return;
    const rowEl = btn.closest('.chat-msg');
    const row = rowEl?._chatRow;
    if (!row || row.pending) return;
    e.preventDefault();
    e.stopPropagation();
    const r = btn.getBoundingClientRect?.();
    openChatRowMenu(row, r ? r.left : 0, r ? r.bottom + 4 : 0, hooks);
  });
  // Touch: long-press or double-tap on a settled bubble opens it at the touch point.
  let touchRow = null;
  const gesture = touchMenuGesture((x, y) => {
    if (touchRow) openChatRowMenu(touchRow, x, y, hooks);
  });
  transcript.addEventListener('touchstart', (e) => {
    const t = e.touches?.[0];
    const rowEl = e.target?.closest?.('.chat-msg');
    const row = rowEl?._chatRow;
    if (!t || e.touches.length > 1 || !row || row.pending || !transcript.contains(rowEl)) {
      gesture.cancel();
      touchRow = null;
      return;
    }
    touchRow = row;
    gesture.start(rowEl, t.clientX, t.clientY);
  }, { passive: true });
  transcript.addEventListener('touchmove', (e) => {
    const t = e.touches?.[0];
    if (t) gesture.move(t.clientX, t.clientY);
  }, { passive: true });
  // Suppress the native menu / selection callout ONLY when ours actually opened.
  transcript.addEventListener('touchend', (e) => { if (gesture.end()) e.preventDefault(); });
  transcript.addEventListener('touchcancel', () => gesture.cancel());
};

// The "Reconnect" call-to-action under an EXPIRED-session message: opens the
// connections modal, where that server's row offers the sign-in again. The URL rides
// the label so a multi-server user knows which session died.
export const chatReconnectButton = (serverUrl, onReconnect) => {
  const b = document.createElement('button');
  b.className = 'btn-icon-text chat-reconnect-cta';
  const host = String(serverUrl || '').replace(/^https?:\/\//i, '');
  b.innerHTML = icon('link', { size: 13 }) + `<span>Reconnect${host ? ` to ${host}` : ''}</span>`;
  b.addEventListener('click', () => onReconnect?.(serverUrl));
  return b;
};

// The "Configure provider" call-to-action shown under an unreachable-provider
// message: opens the LLM settings modal through its own gear button.
export const chatConfigureButton = (onBeforeOpen) => {
  const cfg = document.createElement('button');
  cfg.className = 'btn-icon-text chat-config-cta';
  cfg.innerHTML = icon('gear', { size: 13 }) + '<span>Configure provider</span>';
  cfg.addEventListener('click', () => {
    onBeforeOpen?.();
    document.getElementById('chat-settings-btn')?.click();
  });
  return cfg;
};
