// ── Transcript row DOM: classes, text node, shrink-wrap ─────────
// The per-row bits renderChatLog builds on. Repaints rewrite className wholesale, so the
// motion classes motion.js owns are preserved here rather than in the renderer.
import { CHAT_ENTERING_CLASS, REVEAL_ENTERING_CLASS, REVEAL_IN_CLASS, REVEAL_ITEM_CLASS, REVEAL_MASKED_CLASS, REVEAL_NO_TRIGGER_CLASS, REVEAL_SMOOTH_CLASS } from './motion.js';

// Render the SHARED transcript log (js/llm/chatSession.js) into `transcript`.
// Reset a row's classes without dropping the motion classes motion.js owns — repaints
// rewrite className wholesale, which would strand a revealed row at its dimmed rest state.
// CHAT_ENTERING_CLASS is in here for the same reason and it MATTERS: one turn appends two
// rows (the message, then the pending "…"), so the second append repaints the first — and
// without this the user's own bubble had its veil torn off a frame after it went on, and
// appeared instantly while its dust was still flying (reported on Retry, where the eye is
// already on that bubble).
const MOTION_CLASSES = [REVEAL_ITEM_CLASS, REVEAL_IN_CLASS, REVEAL_MASKED_CLASS, REVEAL_ENTERING_CLASS,
  REVEAL_SMOOTH_CLASS, REVEAL_NO_TRIGGER_CLASS, CHAT_ENTERING_CLASS];
export const setRowClass = (el, cls) => {
  const keep = MOTION_CLASSES.filter((k) => el.classList.contains(k));
  el.className = cls;
  if (keep.length) el.classList.add(...keep);
};

// A row's text lives in ONE dedicated child, never as loose text beside the buttons
// the row also carries — so "a bubble shows its text exactly once" is structural.
export const rowTextNode = (el) => {
  let t = el.querySelector('.chat-msg-text');
  if (!t) {
    t = document.createElement('div');
    t.className = 'chat-msg-text';
    el.prepend(t);
  }
  return t;
};

// ── A wrapped bubble hugs its LONGEST LINE, not the max-width cap ───────────────────
// A block that must wrap never searches for a narrower box that still breaks the same
// way — CSS just gives it the full space .chat-msg's max-width allows, and the shorter
// line is left stranded in dead space (user report: "message width is adjusted wrong").
// Freezing the text at its own widest rendered line reproduces the IDENTICAL break —
// line-breaking is a deterministic left-to-right greedy scan, so a re-wrap at that exact
// width chooses the same points — nothing about the text moves, only the bubble's excess
// goes away. Pure: given the per-line widths a wrapped node's Range reports, the width to
// pin it at; null for one line, which already hugs its own content correctly.
export const shrinkWrapWidth = (lineWidths) => {
  if (!Array.isArray(lineWidths) || lineWidths.length < 2) return null;
  const max = Math.max(...lineWidths);
  return max > 0 ? Math.ceil(max) : null;
};

// A max-width, not a fixed width: a later narrower resize (the dock dragged in, the
// window shrunk) still reflows normally under it; only a WIDER one leaves an old bubble
// conservatively wrapped rather than re-claiming the new room — bindShrinkWrapResize
// below re-measures every row once the transcript itself changes size, which covers that
// case too. Guarded for the DOM-lite test tree, which has no Range.
const measureShrinkWrap = (el) => {
  if (!el.firstChild) return null;
  const range = document.createRange();
  range.selectNodeContents(el);
  return shrinkWrapWidth([...range.getClientRects()].map((r) => r.width));
};

export const applyShrinkWrap = (el) => {
  if (!el?.style || typeof document.createRange !== 'function') return;
  el.style.maxWidth = '';   // drop any earlier pin before re-measuring the natural wrap
  const width = measureShrinkWrap(el);
  if (width != null) el.style.maxWidth = `${width}px`;
};

// A row rendered while the panel was CLOSED (a background turn landing off-screen —
// closedTurnToast exists for exactly that case) measures zero rects at paint time
// (display:none) and skips its pin; this re-measures every settled row's text the
// moment the transcript itself gains — or changes — a real size, so opening the panel
// or dragging its dock/float edge catches every bubble the inline call above missed.
// Bound once per transcript, alongside observeReveal below.
export const bindShrinkWrapResize = (transcript) => {
  if (transcript._shrinkWrapBound || typeof ResizeObserver === 'undefined') return;
  transcript._shrinkWrapBound = true;
  let raf = 0;
  const reapply = () => {
    raf = 0;
    if (typeof document.createRange !== 'function') return;
    const rows = [...transcript.querySelectorAll('.chat-msg-text')].filter((t) => t?.style);
    // Clear-all, measure-all, apply-all: the per-row write→read interleave cost two
    // reflows per row on every pass while the panel edge was being dragged.
    for (const t of rows) t.style.maxWidth = '';
    const widths = rows.map(measureShrinkWrap);
    rows.forEach((t, i) => { if (widths[i] != null) t.style.maxWidth = `${widths[i]}px`; });
  };
  new ResizeObserver(() => { if (!raf) raf = requestAnimationFrame(reapply); }).observe(transcript);
};
