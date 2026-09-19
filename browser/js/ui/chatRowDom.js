// Transcript row DOM: classes, text node, shrink-wrap.
import { CHAT_ENTERING_CLASS, REVEAL_ENTERING_CLASS, REVEAL_IN_CLASS, REVEAL_ITEM_CLASS, REVEAL_MASKED_CLASS, REVEAL_NO_TRIGGER_CLASS, REVEAL_SMOOTH_CLASS } from './motion.js';

// Repaints rewrite className wholesale, so motion.js's classes are kept here — CHAT_ENTERING_CLASS
// too: a turn appends two rows, and the second repaint would tear the first's veil off mid-flight.
const MOTION_CLASSES = [REVEAL_ITEM_CLASS, REVEAL_IN_CLASS, REVEAL_MASKED_CLASS, REVEAL_ENTERING_CLASS,
  REVEAL_SMOOTH_CLASS, REVEAL_NO_TRIGGER_CLASS, CHAT_ENTERING_CLASS];
export const setRowClass = (el, cls) => {
  const keep = MOTION_CLASSES.filter((k) => el.classList.contains(k));
  el.className = cls;
  if (keep.length) el.classList.add(...keep);
};

// A row's text lives in one dedicated child, so "a bubble shows its text exactly once" is structural.
export const rowTextNode = (el) => {
  let t = el.querySelector('.chat-msg-text');
  if (!t) {
    t = document.createElement('div');
    t.className = 'chat-msg-text';
    el.prepend(t);
  }
  return t;
};

// A wrapped bubble hugs its longest line, not the max-width cap: line-breaking is a
// deterministic greedy scan, so a re-wrap at exactly that width breaks the same way.
export const shrinkWrapWidth = (lineWidths) => {
  if (!Array.isArray(lineWidths) || lineWidths.length < 2) return null;
  const max = Math.max(...lineWidths);
  return max > 0 ? Math.ceil(max) : null;
};

// A max-width, not a fixed width, so a narrower resize still reflows; a wider one is
// covered by bindShrinkWrapResize. Guarded for the DOM-lite test tree (no Range).
const measureShrinkWrap = (el) => {
  if (!el.firstChild) return null;
  const range = document.createRange();
  range.selectNodeContents(el);
  return shrinkWrapWidth([...range.getClientRects()].map((r) => r.width));
};

export const applyShrinkWrap = (el) => {
  if (!el?.style || typeof document.createRange !== 'function') return;
  el.style.maxWidth = '';
  const width = measureShrinkWrap(el);
  if (width != null) el.style.maxWidth = `${width}px`;
};

// A row rendered while the panel was closed measures zero rects (display:none) and skips
// its pin; re-measure every settled row when the transcript gains or changes a real size.
export const bindShrinkWrapResize = (transcript) => {
  if (transcript._shrinkWrapBound || typeof ResizeObserver === 'undefined') return;
  transcript._shrinkWrapBound = true;
  let raf = 0;
  const reapply = () => {
    raf = 0;
    if (typeof document.createRange !== 'function') return;
    const rows = [...transcript.querySelectorAll('.chat-msg-text')].filter((t) => t?.style);
// Clear-all, measure-all, apply-all: interleaving cost two reflows per row per pass.
    for (const t of rows) t.style.maxWidth = '';
    const widths = rows.map(measureShrinkWrap);
    rows.forEach((t, i) => { if (widths[i] != null) t.style.maxWidth = `${widths[i]}px`; });
  };
  new ResizeObserver(() => { if (!raf) raf = requestAnimationFrame(reapply); }).observe(transcript);
};
