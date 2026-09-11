// ── Rendering the SHARED transcript log ─────────────────────────
// Keyed by row id and incremental: existing rows update in place. Model output is DATA —
// every string lands via textContent, never innerHTML.
import { LEAVING_CLASS, chatIn, observeReveal, wipeDurationMs } from './motion.js';
import { applyShrinkWrap, bindShrinkWrapResize, rowTextNode, setRowClass } from './chatRowDom.js';
import { chatAskCard, chatAttachmentStrip, chatConfigureButton, chatReconnectButton, chatResultCard } from './chatCards.js';
import { chatEmptyState, typingDots } from './chatEmpty.js';
import { chatLeave } from './chatLeave.js';
import { chatRowMenuButton } from './chatRowMenuModel.js';
import { icon } from './icons.js';

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
  bindShrinkWrapResize(transcript);
  // Everything that APPEARS in this repaint, collected and played out at the END: the
  // dust is a CLONE of the entry, so it can only be taken once the row is fully built
  // (its text, its CTAs, its "…"), and the count is what budgets a burst's mesh.
  // The FIRST paint of a transcript is deliberately silent — a surface opening onto
  // history it missed is not a conversation happening in front of you.
  const entering = [];
  const enters = (el) => { if (transcript._chatPainted) entering.push(el); };
  const live = new Set();
  for (const row of log) {
    live.add(String(row.id));
    let el = transcript.querySelector(`[data-row="${row.id}"]`);
    if (!el) {
      el = document.createElement('div');
      el.dataset.row = row.id;
      transcript.appendChild(el);
      // …but NOT a row born pending: the "…" is a placeholder that lives about as long as
      // the gather itself, so dusting it in kept it veiled for almost its whole life and
      // the bouncing dots were never seen. Its arrival is the SETTLE below — the reply
      // taking their place is the thing worth animating.
      if (!row.pending) enters(el);
    }
    // …and a settling turn is an arrival too: the reply (or the failure) takes the place
    // the bouncing dots held, in the SAME element, so nothing above would catch it.
    if (el._chatPending && !row.pending) enters(el);
    el._chatPending = !!row.pending;
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
    } else if (typing || textEl.textContent !== row.text) {
      textEl.textContent = row.text;
      applyShrinkWrap(textEl);
    }
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
      retry.dataset.title = 'Send this message again';
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
        enters(strip);
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
        enters(cards);
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
        enters(askEl);
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
  // …and only THEN the arrivals (motion.js chatIn), last of all: every entry is fully
  // built by now (the dust is a clone, so a cloud taken mid-build would be missing the
  // row's own text and CTAs) and the transcript has been told to scroll. chatIn veils
  // each entry at once and waits two frames before photographing it, so what it measures
  // is the settled box — the height is allocated and scrolled to first, the motes fly
  // second. Sharing one grid budget with the wipe above: a Clear that also lands a fresh
  // turn must not put two full meshes in the air at once.
  entering.forEach((el, i) => chatIn(el, entering.length + going.length, i));
  transcript._chatPainted = true;
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
