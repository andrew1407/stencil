// ── Transcript rendering (textContent only — LLM output is data) ────────────
// Every entry the panel can put on screen — messages, warnings, cards, the attachment
// strip, the in-flight dots, the empty-state chips — plus the clear that takes them all
// away. Shared mutable state (busy, the controller, the working scan, the per-message
// menu hook) lives on `state`; everything else arrives as a collaborator.
import { icon } from '../../lib/icons.js';
import { setTip } from '../../lib/tip.js';
import { makeDismissible, renderSuggestions, wireThumbPreview, applyShrinkWrap } from '../../lib/chatUi.js';
import { wipeDurationMs } from '../../lib/motion.js';
import { chatLeave, chatEnter } from './shared.js';

export const createTranscript = ({ sectionEl, transcriptEl, inputEl, tray, state }) => {
  const { pending, renderTray, syncClearBtn } = tray;

  // Clear = fresh conversation: model history + transcript + queued attachments.
  // The empty-state suggestion chips come back (browser parity).
  const clearConversation = () => {
    if (state.busy) return;
    state.controller.clearConversation();
    state.workingScan = null;   // back to chatting about the panel's own scan
    // Entries are removed individually, NOT by wiping textContent: the scatter's particle layer is
    // itself a child of the transcript.
    const going = [...transcriptEl.children].filter((el) => !el.classList.contains('disintegrate-host'));
    const wiped = going.length > 0;
    going.forEach((el, i) => chatLeave(el, () => el.remove(), going.length, i));
    pending.splice(0);
    renderTray();
    // The empty state comes back only once the entries have finished leaving (browser chatView.js
    // restoreEmptyState sequences it the same way).
    if (wiped) {
      setTimeout(() => {
        // `:scope >` like syncClearBtn: the scatter's clones keep the .msg class, so an unscoped query
        // would read the dying copies as live conversation.
        if (state.busy || transcriptEl.querySelector(':scope > .msg, :scope > .card, :scope > .warn')) return;
        showSuggestions();
        syncClearBtn();
      }, wipeDurationMs());
    } else {
      showSuggestions();
    }
    syncClearBtn();
    inputEl.focus();
  };

  // A fresh row grows AFTER the first measure, so the pin is repeated once the entrance settles
  // (browser chatView stickToBottom parity).
  const scrollDown = () => {
    const pin = () => { transcriptEl.scrollTop = transcriptEl.scrollHeight; };
    pin();
    if (typeof requestAnimationFrame === 'function') requestAnimationFrame(pin);
    setTimeout(pin, 220);
  };

  // `arrive: false` opts out of the dust (the in-flight "…" — see addThinking); `wrap` pins a
  // text bubble to its longest line (chatUi.js).
  const appendEntry = (node, { arrive = true, wrap = false } = {}) => {
    transcriptEl.appendChild(node);
    if (wrap) applyShrinkWrap(node);
    syncClearBtn();
    scrollDown();
    if (arrive) chatEnter(node, sectionEl);
    return node;
  };

  const appendDiv = (className, text, { arrive = true } = {}) => {
    const div = document.createElement('div');
    div.className = className;
    div.textContent = text;
    return appendEntry(div, { arrive, wrap: true });
  };

  // Side-notes stay DISMISSABLE; error messages carry no × — the browser panel renders a failed
  // turn as a plain bubble with the retry only.
  const dismissible = (el, autoMs = 0) => {
    makeDismissible(el, { doc: document, autoMs });
    return el;
  };

  // What the per-message context menu acts on: the message's original text (and, for
  // user turns, the attachments that rode it — kept so Resend can requeue them).
  const msgMeta = new WeakMap();
  const addMsg = (cls, text) => {
    const el = appendDiv(`msg ${cls}`, text);
    msgMeta.set(el, { text });
    if (cls !== 'note') state.addMsgMenuBtn(el);   // notes (and the typing row) get no menu
    return el;
  };
  const addWarn = (text) => dismissible(appendDiv('warn', text));

  // Browser chatView.js chatAttachmentStrip parity. Names come from scanned pages, so they stay
  // DATA — alt/title only, never markup.
  const addAttachments = (list) => {
    const strip = document.createElement('div');
    strip.className = 'chat-attached';
    for (const p of list) {
      const img = document.createElement('img');
      img.className = 'chat-attached-thumb';
      img.src = `data:${p.image.mediaType};base64,${p.image.data}`;
      img.alt = p.name;
      const caption = Number.isInteger(p.index) ? `image #${p.index} from the page (${p.name})` : p.name;
      setTip(img, caption);
      // 56px is too small to tell two screenshots apart — hovering shows it big.
      wireThumbPreview(img, { caption });
      strip.appendChild(img);
    }
    return appendEntry(strip);
  };

  // The failed bubble STAYS — a retry is another attempt, not an undo, and the transcript records
  // both (browser chatView.js onRetry, desktop chatRetryTurn). Nothing is auto-retried.
  const addRetry = (el, text, send, attachments = []) => {
    const retry = document.createElement('button');
    retry.className = 'chat-retry';
    setTip(retry, 'Send this message again');
    retry.setAttribute('aria-label', 'Retry');
    retry.innerHTML = icon('refresh', { size: 13 });
    retry.addEventListener('click', () => { if (!state.busy) send(text, attachments); });
    el.appendChild(retry);
    applyShrinkWrap(el);   // re-measure now the icon rides beside the pinned text
  };

  // Browser chatConfigureButton parity: carried only when the PROVIDER itself is the problem.
  // Opens the same Options page the "…" menu's Settings item does.
  const addConfigureCta = (el) => {
    const cfg = document.createElement('button');
    cfg.className = 'btn-icon-text chat-config-cta';
    cfg.innerHTML = icon('gear', { size: 13 }) + '<span>Configure provider</span>';
    cfg.addEventListener('click', () => { document.getElementById('open-options')?.click(); });
    el.appendChild(cfg);
    applyShrinkWrap(el);   // never pin narrower than the button that just rode in
  };

  // Browser chatView.js typingDots parity: the shape the reply will take, in its place.
  const addThinking = () => {
    // No arrival for the placeholder (browser chatView.js parity): it lives about as long as the
    // gather itself, so dusting it in kept the bouncing dots veiled for almost its whole life.
    const el = appendDiv('msg assistant typing-row', '', { arrive: false });
    const dots = document.createElement('span');
    dots.className = 'chat-typing';
    dots.setAttribute('role', 'status');
    dots.setAttribute('aria-label', 'Assistant is answering');
    for (let i = 0; i < 3; i++) dots.appendChild(document.createElement('i'));
    el.appendChild(dots);
    scrollDown();
    return el;
  };

  // `label` is plain text — entry names come from scanned pages. `autoMs` (attach failures)
  // clears the card after a few seconds.
  const addCard = (iconName, label, ok = true, autoMs = 0) => {
    const div = document.createElement('div');
    div.className = 'card' + (ok ? '' : ' fail');
    div.innerHTML = icon(ok ? iconName : 'x', { size: 14 });   // fixed glyph, no user data
    const span = document.createElement('span');
    span.textContent = label;
    div.appendChild(span);
    if (!ok) makeDismissible(div, { doc: document, autoMs });
    return appendEntry(div);
  };

  // Clicking a chip PREFILLS the input — it never sends (browser parity, §8 wording). The block
  // goes with the first message and returns when the conversation is cleared.
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

  return { clearConversation, scrollDown, appendEntry, appendDiv, dismissible, msgMeta,
           addMsg, addWarn, addAttachments, addRetry, addConfigureCta, addThinking, addCard,
           hideSuggestions, showSuggestions };
};
