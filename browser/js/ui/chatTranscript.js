// Rendering the shared transcript log. Model output is DATA — every string lands via
// textContent, never innerHTML.
import { LEAVING_CLASS, chatIn, observeReveal, wipeDurationMs } from './motion.js';
import { applyShrinkWrap, bindShrinkWrapResize, rowTextNode, setRowClass } from './chatRowDom.js';
import { chatAskCard, chatAttachmentStrip, chatConfigureButton, chatReconnectButton, chatResultCard } from './chatCards.js';
import { chatEmptyState, typingDots } from './chatEmpty.js';
import { chatLeave } from './chatLeave.js';
import { chatRowMenuButton } from './chatRowMenuModel.js';
import { icon } from './icons.js';

// Keyed by row id and incremental; both surfaces call this on every log change.
export const renderChatLog = (transcript, log, { onConfigure, onAskSubmit, onRetry, onReconnect } = {}) => {
// Stickiness measured before the mutations: follow only when already near the bottom.
  const stick = transcript.scrollHeight - transcript.clientHeight - transcript.scrollTop < 40;
// An empty conversation shows the chips; bringing them back waits for the rows to leave.
  if (log.length) transcript.querySelector('.chat-empty')?.remove();
// One reveal observer per transcript. smooth: a grainy dissolve on text reads as corruption.
  if (!transcript._revealBound) transcript._revealBound = observeReveal(transcript, '[data-row]', { smooth: true });
  bindShrinkWrapResize(transcript);
// Arrivals are played at the end: the dust is a clone, so the row must be fully built,
// and the count budgets the burst's mesh. The first paint of a transcript is silent.
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
// Not a row born pending: the "…" placeholder lives about as long as the gather. Its
// arrival is the settle below.
      if (!row.pending) enters(el);
    }
// A settling turn is an arrival too: the reply takes the dots' place in the same element.
    if (el._chatPending && !row.pending) enters(el);
    el._chatPending = !!row.pending;
// Part of the class string: setRowClass rewrites className wholesale on every repaint.
    const retryable = !!(row.error && row.retryText && onRetry);
    setRowClass(el, `chat-msg chat-msg-${row.role}`
      + (row.error ? ' chat-msg-error' : '')
      + (row.card ? ' chat-error-card' : '')
      + (retryable ? ' chat-msg-cta' : ''));
    el._chatRow = row;
// Both land in the row's one text node, only when changed: a rewrite per repaint would
// tear a held selection out of the user's hands.
    const textEl = rowTextNode(el);
    const typing = textEl.querySelector('.chat-typing');
    if (row.pending) {
      if (!typing) { textEl.textContent = ''; textEl.appendChild(typingDots()); }
// A settled row must lose the dots even when its text is '' (no "change" to see).
    } else if (typing || textEl.textContent !== row.text) {
      textEl.textContent = row.text;
      applyShrinkWrap(textEl);
    }
// Built once. An expired session takes the reconnect CTA: the provider is fine, the token is dead.
    const cta = row.reconnect ? '.chat-reconnect-cta' : '.chat-config-cta';
    if (row.card && !el.querySelector(cta)) {
      el.appendChild(row.reconnect
        ? chatReconnectButton(row.reconnect, onReconnect)
        : chatConfigureButton(onConfigure));
    }
    if (!row.card || row.reconnect) el.querySelector('.chat-config-cta')?.remove();
    if (!row.card || !row.reconnect) el.querySelector('.chat-reconnect-cta')?.remove();
// Retry sends the same text through the normal path; built once, since the row's buttons
// survive a repaint.
    if (retryable && !el.querySelector('.chat-retry-cta')) {
// Icon-only: a labelled button reads as part of the message.
      const retry = document.createElement('button');
      retry.className = 'chat-hbtn chat-retry-cta';
      retry.dataset.title = 'Send this message again';
      retry.setAttribute('aria-label', 'Retry');
      retry.innerHTML = icon('refresh', { size: 13 });
      retry.addEventListener('click', () => onRetry(row.retryText));
      el.appendChild(retry);
    }
// Built once; every settled row gets one, the first included.
    if (!el.querySelector('.chat-row-menu-btn')) {
      const more = chatRowMenuButton(row);
      if (more) el.appendChild(more);
    }
// Attachments ride in their own row just before the message, built once (no thumbnail reload).
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
      cards.removeAttribute('data-row');
      chatLeave(cards, () => cards.remove());
    }
// The §11 choice card, built once: a rebuild would wipe a half-made selection.
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
// Dropped rows dissolve; one already leaving is skipped, or re-arming would restart it forever.
  const going = [...transcript.querySelectorAll('[data-row]')].filter((el) => !live.has(el.dataset.row));
  const wiped = going.length > 0;
  going.forEach((el, i) => {
    el.removeAttribute('data-row');
    chatLeave(el, () => el.remove(), going.length, i);
  });
  restoreEmptyState(transcript, log, wiped);
  if (stick) stickToBottom(transcript);
// Arrivals last of all: every entry is fully built and the transcript has been told to
// scroll (chatIn waits two frames before photographing). One grid budget shared with
// the wipe above.
  entering.forEach((el, i) => chatIn(el, entering.length + going.length, i));
  transcript._chatPainted = true;
};

// Pin to the bottom now and again as the entrances settle: a fresh row grows after the
// first measure.
export const stickToBottom = (transcript) => {
  const pin = () => { transcript.scrollTop = transcript.scrollHeight; };
  pin();
  if (typeof requestAnimationFrame === 'function') requestAnimationFrame(pin);
  setTimeout(pin, 220);
};

// Bring the empty state back only once the transcript has emptied on screen (rows first,
// then the chips). One waiter, re-checking before painting.
const restoreEmptyState = (transcript, log, wiped) => {
  const paint = () => {
    transcript._emptyWaiting = false;
// The conversation may have restarted while the particles fell.
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

// On the transcript itself: the .chat-empty block is replaced whenever the conversation empties.
export const wireChatSuggestions = (transcript, onPick) => {
  transcript.addEventListener('click', (e) => {
    const chip = e.target.closest('.chat-suggest');
    if (chip && transcript.contains(chip)) onPick(chip.dataset.prompt);
  });
};
