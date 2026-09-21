import { leaveThenRemove } from '../motion.js';
import { setTip } from '../tip/tip.js';
// Assistant transcript widgets, DOM-injected so `node --test` drives them with a stub.

// Attach-failure cards clear themselves after this long; everything else waits for the ×.
export const AUTO_DISMISS_MS = 8000;

// `autoMs` 0/absent = manual only.
export const makeDismissible = (el, { doc, autoMs = 0, timer = setTimeout, onDismiss } = {}) => {
  const button = doc.createElement('button');
  button.type = 'button';
  button.className = 'x-dismiss';
  setTip(button, 'Dismiss');
  button.textContent = '×';
  if (button.setAttribute) button.setAttribute('aria-label', 'Dismiss');

  let gone = false;
  const dismiss = () => {
    if (gone) return;
    gone = true;
    leaveThenRemove(el, () => {
      el.remove();
      if (onDismiss) onDismiss();
    });
  };
  button.addEventListener('click', (e) => {
    if (e && e.stopPropagation) e.stopPropagation();
    dismiss();
  });
  el.appendChild(button);
  // `isConnected === false`: already dismissed or cleared, so no onDismiss.
  if (autoMs > 0) timer(() => { if (el.isConnected !== false) dismiss(); }, autoMs);
  return { dismiss, button };
};

// Hover preview for a 28–56px thumbnail, appended to the BODY because the popup's
// regions scroll and would clip it. Mirror of the browser's view.js wireThumbPreview.
let openPreview = null;
export const hideThumbPreview = () => { openPreview?.remove?.(); openPreview = null; };
// Alt HELD doubles the glance; pressed or released mid-hover it re-places itself.
const altPreview = (on) => {
  if (!openPreview?.classList) return;
  openPreview.classList.toggle('chat-thumb-preview-xl', on);
  openPreview.__place?.();
};
globalThis.addEventListener?.('keydown', (e) => { if (e.key === 'Alt') altPreview(true); });
globalThis.addEventListener?.('keyup', (e) => { if (e.key === 'Alt') altPreview(false); });
export const wireThumbPreview = (img, { doc = globalThis.document, caption = '', src } = {}) => {
  if (!img?.addEventListener || !doc?.createElement) return;
  const show = (e) => {
    hideThumbPreview();
    const box = doc.createElement('div');
    box.className = 'chat-thumb-preview';
    if (e?.altKey && box.classList) box.classList.add('chat-thumb-preview-xl');
    const big = doc.createElement('img');
    big.src = src || img.src;
    big.alt = '';
    box.appendChild(big);
    if (caption) {
      const cap = doc.createElement('span');
      cap.className = 'chat-thumb-preview-cap';
      cap.textContent = caption;   // a scanned filename — data, never markup
      box.appendChild(cap);
    }
    doc.body.appendChild(box);
    openPreview = box;
    const place = () => {
      if (!img.getBoundingClientRect || !box.getBoundingClientRect) return;
      const r = img.getBoundingClientRect();
      const b = box.getBoundingClientRect();
      const vw = globalThis.innerWidth || 0;
      const vh = globalThis.innerHeight || 0;
      const left = Math.max(8, Math.min(r.left, vw - b.width - 8));
      const above = r.top - b.height - 10;
      const top = above >= 8 ? above : Math.min(r.bottom + 10, vh - b.height - 8);
      box.style.left = `${Math.round(left)}px`;
      box.style.top = `${Math.round(Math.max(8, top))}px`;
      box.classList.add('chat-thumb-preview-in');
    };
    box.__place = place;
    if (big.complete) place(); else big.addEventListener('load', place, { once: true });
  };
  img.addEventListener('mouseenter', show);
  img.addEventListener('mouseleave', hideThumbPreview);
  img.addEventListener('click', hideThumbPreview);
  return { show, hide: hideThumbPreview };
};

// A wrapped bubble hugs its LONGEST LINE, not the max-width cap (port of browser
// view.js shrinkWrapWidth). Null for one line.
export const shrinkWrapWidth = (lineWidths) => {
  if (!Array.isArray(lineWidths) || lineWidths.length < 2) return null;
  const max = Math.max(...lineWidths);
  return max > 0 ? Math.ceil(max) : null;
};

// A bubble's own horizontal padding + border, the part a border-box width must carry.
const frameWidth = (el, doc) => {
  const cs = doc?.defaultView?.getComputedStyle?.(el);
  if (!cs) return 0;
  const px = (v) => parseFloat(v) || 0;
  return Math.ceil(px(cs.paddingLeft) + px(cs.paddingRight) + px(cs.borderLeftWidth) + px(cs.borderRightWidth));
};

// Measured from `el`'s FIRST child only (its text node); element children floor the pin.
export const applyShrinkWrap = (el, doc = globalThis.document) => {
  if (!el?.style || !doc?.createRange) return;
  el.style.maxWidth = '';   // drop any earlier pin before re-measuring the natural wrap
  const textNode = el.firstChild;
  // Only a TEXT first child measures as lines: the typing row's dots would pin the
  // bubble to one dot, leaving the other two outside it.
  if (!textNode || textNode.nodeType !== 3) return;
  const range = doc.createRange();
  range.selectNodeContents(textNode);
  let width = shrinkWrapWidth([...range.getClientRects()].map((r) => r.width));
  if (width == null) return;
  for (const child of el.children || []) {
    const w = child.getBoundingClientRect?.().width;
    if (w > width) width = Math.ceil(w);
  }
  // A border-box pin (theme/controls.css) must carry the bubble's own frame, else the
  // text re-wraps narrower than the line it was measured from.
  el.style.maxWidth = `${width + frameWidth(el, doc)}px`;
};

// A bubble rendered while collapsed measures zero rects and skips its pin; re-measure
// every bubble whenever the transcript gains or changes a real size.
export const bindShrinkWrapResize = (transcript, selector = '.msg', doc = globalThis.document) => {
  if (!transcript || transcript._shrinkWrapBound || typeof ResizeObserver === 'undefined') return;
  transcript._shrinkWrapBound = true;
  let raf = 0;
  const reapply = () => {
    raf = 0;
    for (const el of transcript.querySelectorAll(selector)) applyShrinkWrap(el, doc);
  };
  new ResizeObserver(() => { if (!raf) raf = requestAnimationFrame(reapply); }).observe(transcript);
};

// `prompt` is what lands in the input — never sent by itself.
export const SUGGESTIONS = Object.freeze([
  { label: 'Which of these has a cat?', prompt: 'Which of these images has a cat in it?' },
  { label: 'Find the largest image', prompt: 'Find the largest image on this page and tell me its size' },
  { label: 'Open the first photo in the editor', prompt: 'Open the first photo on this page in the editor' },
  { label: 'Describe the chart', prompt: 'Describe the chart or diagram on this page' },
]);

export const renderSuggestions = (doc, onPick, items = SUGGESTIONS) => {
  const wrap = doc.createElement('div');
  wrap.className = 'chat-empty';
  for (const s of items) {
    const b = doc.createElement('button');
    b.type = 'button';
    b.className = 'chat-suggest';
    b.textContent = s.label;
    // No tooltip: the chip IS its own label, and a bubble would cover the chips beside it.
    if (b.dataset) b.dataset.prompt = s.prompt;
    b.addEventListener('click', () => onPick(s.prompt));
    wrap.appendChild(b);
  }
  return wrap;
};
