import { leaveThenRemove } from './motion.js';
import { setTip } from './tip.js';
// ── Assistant transcript widgets (pure, unit-testable) ──────────────────────
// Two small DOM builders the embedded Assistant section (src/popup/assistant.js)
// uses, kept out of that chrome/DOM-bound module so `node --test` can drive them
// with a stub document:
//   makeDismissible  — a × button on an error/notice entry, so failures don't pile
//                      up in the transcript forever (with optional auto-dismiss)
//   renderSuggestions— the empty-state prompt chips (browser parity:
//                      browser/js/ui/chatPanel.js .chat-empty / .chat-suggest),
//                      worded for the EXTENSION profile (contract §8: the working
//                      set is the page's scanned images, and the ops are
//                      focus/open/attach — never an image edit here).
// The `doc` seam is always injected; nothing here touches globals.

// Attach-failure cards clear themselves after this long; everything else waits
// for the ×.
export const AUTO_DISMISS_MS = 8000;

/**
 * Add a small × dismiss button to a transcript entry. `autoMs` 0/absent = manual only;
 * `doc` and `timer` are the injected seams.
 * @returns {{dismiss: Function, button: object}}
 */
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
    // Dissolve first, then remove (lib/motion.js). The `gone` latch already makes
    // this idempotent, so a second click during the play-out is a no-op.
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
  // `isConnected === false` means the user already dismissed it (or the transcript
  // was cleared) — don't fire onDismiss for an entry that's already gone.
  if (autoMs > 0) timer(() => { if (el.isConnected !== false) dismiss(); }, autoMs);
  return { dismiss, button };
};

// ── Hover preview for a small attachment thumbnail ──────────────────────────
// The chips and the transcript strip show a picture at 28–56px, which is too small to
// tell two screenshots apart; hovering shows it at a readable size. The preview is
// appended to the BODY (the popup is 400px wide and its regions scroll, so an
// in-place popup would be clipped exactly when it matters) and flips above/below to
// stay in view. `doc` is injectable, like makeDismissible, so tests drive it with a
// stub document. Mirror of the browser's chatView.js wireThumbPreview.
let openPreview = null;
export const hideThumbPreview = () => { openPreview?.remove?.(); openPreview = null; };
// Alt HELD doubles the glance (browser chatView parity) — pressed or released
// mid-hover it resizes in place and re-places itself to stay in view. Guarded:
// node's test globalThis has no addEventListener.
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
    if (e?.altKey && box.classList) box.classList.add('chat-thumb-preview-xl');   // Alt held on entry
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
    box.__place = place;   // the Alt resize re-places the open box
    if (big.complete) place(); else big.addEventListener('load', place, { once: true });
  };
  img.addEventListener('mouseenter', show);
  img.addEventListener('mouseleave', hideThumbPreview);
  img.addEventListener('click', hideThumbPreview);
  return { show, hide: hideThumbPreview };
};

// ── A wrapped bubble hugs its LONGEST LINE, not the max-width cap ───────────────────
// Port of browser chatView.js shrinkWrapWidth/applyShrinkWrap: pinning a bubble at its
// widest rendered line reproduces the identical break, minus the dead space. Pure:
// per-line widths in, the width to pin at out; null for one line (already hugging).
export const shrinkWrapWidth = (lineWidths) => {
  if (!Array.isArray(lineWidths) || lineWidths.length < 2) return null;
  const max = Math.max(...lineWidths);
  return max > 0 ? Math.ceil(max) : null;
};

// A max-width, not a fixed width, measured from `el`'s FIRST child only (its text node);
// element children (CTA buttons) instead floor the pin, and callers re-apply after
// appending one. `doc` injected; no-ops without Range (the node --test stub tree).
export const applyShrinkWrap = (el, doc = globalThis.document) => {
  if (!el?.style || !doc?.createRange) return;
  el.style.maxWidth = '';   // drop any earlier pin before re-measuring the natural wrap
  const textNode = el.firstChild;
  if (!textNode) return;
  const range = doc.createRange();
  range.selectNodeContents(textNode);
  let width = shrinkWrapWidth([...range.getClientRects()].map((r) => r.width));
  if (width == null) return;   // one line already hugs — nothing to freeze
  for (const child of el.children || []) {
    const w = child.getBoundingClientRect?.().width;
    if (w > width) width = Math.ceil(w);
  }
  el.style.maxWidth = `${width}px`;
};

// A row rendered while the section was collapsed, or a turn that landed off-screen
// (closedTurnToast exists for exactly that case), measures zero rects at paint time and
// skips its pin; this re-measures every bubble the moment the transcript itself gains —
// or changes — a real size. Bound once per transcript.
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

// Empty-state prompt chips. `prompt` is what lands in the input (never sent — the
// user edits/sends it); `label` is the short chip text.
export const SUGGESTIONS = [
  { label: 'Which of these has a cat?', prompt: 'Which of these images has a cat in it?' },
  { label: 'Find the largest image', prompt: 'Find the largest image on this page and tell me its size' },
  { label: 'Open the first photo in the editor', prompt: 'Open the first photo on this page in the editor' },
  { label: 'Describe the chart', prompt: 'Describe the chart or diagram on this page' },
];

/**
 * Build the empty-state suggestion-chip block. Clicking a chip calls `onPick(prompt)` —
 * it PREFILLS the input, it never sends. Returns the container to append.
 */
export const renderSuggestions = (doc, onPick, items = SUGGESTIONS) => {
  const wrap = doc.createElement('div');
  wrap.className = 'chat-empty';
  for (const s of items) {
    const b = doc.createElement('button');
    b.type = 'button';
    b.className = 'chat-suggest';
    b.textContent = s.label;
    // No tooltip (browser parity: chatView.js chatEmptyState builds these bare) — the chip
    // IS its own label, and a bubble restating it covers the chips beside it. The full
    // prompt rides `data-prompt`, which is what the click prefills.
    if (b.dataset) b.dataset.prompt = s.prompt;
    b.addEventListener('click', () => onPick(s.prompt));
    wrap.appendChild(b);
  }
  return wrap;
};
