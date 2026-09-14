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

  // Pin now and again after the entrance animation settles — a fresh row (the
  // pending indicator included) grows AFTER the first measure, so one write landed
  // a row short of the bottom (browser chatView stickToBottom parity).
  const scrollDown = () => {
    const pin = () => { transcriptEl.scrollTop = transcriptEl.scrollHeight; };
    pin();
    if (typeof requestAnimationFrame === 'function') requestAnimationFrame(pin);
    setTimeout(pin, 220);
  };

  // One ritual for every transcript arrival: append, sync the clear button, pin the
  // scroll, then dust the entry in. `arrive: false` opts out of the dust (the in-flight
  // "…" — see addThinking); `wrap` pins a text bubble to its longest line (chatUi.js) —
  // cards, strips and ask cards keep their natural width.
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
  const addMsg = (cls, text) => {
    const el = appendDiv(`msg ${cls}`, text);
    msgMeta.set(el, { text });
    if (cls !== 'note') state.addMsgMenuBtn(el);   // notes (and the typing row) get no menu
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
      const caption = Number.isInteger(p.index) ? `image #${p.index} from the page (${p.name})` : p.name;
      setTip(img, caption);
      // 56px is too small to tell two screenshots apart — hovering shows it big.
      wireThumbPreview(img, { caption });
      strip.appendChild(img);
    }
    return appendEntry(strip);
  };

  // Icon-only Retry inside a bubble: re-sends the SAME turn through the normal path, its
  // attachments included (nothing is auto-retried). Failed and stopped turns. The failed
  // bubble STAYS — browser chatView.js onRetry logs a NEW turn, desktop chatRetryTurn just
  // resends: a retry is another attempt, not an undo, and the transcript records both.
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

  // The "Configure provider" call-to-action a failed turn's bubble carries when the
  // PROVIDER itself is the problem (browser chatConfigureButton parity) — unreachable or
  // unconfigured, not merely disabled/truncated/refused. Opens the same Options page the
  // "…" menu's Settings item does (there is no in-panel settings modal here to grow out of
  // the CTA, unlike the browser's docked panel).
  const addConfigureCta = (el) => {
    const cfg = document.createElement('button');
    cfg.className = 'btn-icon-text chat-config-cta';
    cfg.innerHTML = icon('gear', { size: 13 }) + '<span>Configure provider</span>';
    cfg.addEventListener('click', () => { document.getElementById('open-options')?.click(); });
    el.appendChild(cfg);
    applyShrinkWrap(el);   // never pin narrower than the button that just rode in
  };

  // The in-flight turn's placeholder: an assistant BUBBLE holding three bouncing dots
  // — the shape the reply will take, in the place it will take it (browser chatView.js
  // typingDots).
  const addThinking = () => {
    // No arrival for the placeholder (browser chatView.js parity): it lives about as long
    // as the gather itself, so dusting it in kept it veiled for almost its whole life and
    // the bouncing dots were never seen. The reply that replaces it is what arrives.
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
    return appendEntry(div);
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

  return { clearConversation, scrollDown, appendEntry, appendDiv, dismissible, msgMeta,
           addMsg, addWarn, addAttachments, addRetry, addConfigureCta, addThinking, addCard,
           hideSuggestions, showSuggestions };
};
