import { TUNE } from './tune.js';
// Rows only dissolve by the amount the scroller is ALREADY clipping them; a row you can
// see in full is never touched — decoration must never cost legibility.
export const REVEAL_ITEM_CLASS = 'reveal-item';
export const REVEAL_IN_CLASS = 'reveal-in';
export const REVEAL_ENTERING_CLASS = 'reveal-entering';
// Gates the mask itself: only a row straddling an edge is worth masking.
export const REVEAL_MASKED_CLASS = 'reveal-masked';
export const REVEAL_SMOOTH_CLASS = 'reveal-smooth';
export const REVEAL_NO_TRIGGER_CLASS = 'reveal-no-trigger';
export const REVEAL_ENTER_MS = TUNE.REVEAL_ENTER_MS;
// How far the wipe softens into the cut — a share of the ROW, matching animations/reveal.css.
export const REVEAL_FEATHER = TUNE.REVEAL_FEATHER;

// How dissolved a row spanning [top, bottom) is in a scroller `viewH` tall: 0 while
// wholly on screen, rising with the clipped share, 1 once it is gone. Pure.
export const revealDissolve = (top, bottom, viewH) => {
  const h = bottom - top;
  if (viewH <= 0 || h <= 0) return 0;
  const visible = Math.max(0, Math.min(bottom, viewH) - Math.max(top, 0));
  return 1 - visible / h;
};

// Which of a row's edges the scroller is ACTUALLY cutting — the only ones the wipe may
// soften: feathering both ends unconditionally sands content sitting on a fully visible
// edge (the hover "…" trigger). Half-pixel slack: flush with an edge is not clipped. Pure.
export const revealFeather = (top, bottom, viewH, feather = REVEAL_FEATHER) => ({
  in: top < -0.5 ? feather : '0%',
  out: bottom > viewH + 0.5 ? feather : '0%',
});

// Text rows (the chat transcript) fade over a FIXED band instead — see the smooth
// option on observeReveal. A constant is also what the bottom-anchored trigger below
// clears, so the two agree by construction.
export const REVEAL_SMOOTH_FEATHER_PX = TUNE.REVEAL_SMOOTH_FEATHER_PX;
export const REVEAL_SMOOTH_FEATHER = `${REVEAL_SMOOTH_FEATHER_PX}px`;

// Distance UP from the row's own bottom where bottom-anchored chrome (the chat "…"
// trigger) must sit to stay inside the slice the SCROLLER is showing, clearing the cut
// edge's fade band — a CONSTANT band, not a share of the row. Pure.
export const revealVisibleBottom = (top, bottom, viewH) => {
  const h = bottom - top;
  if (h <= 0) return 0;
  const vTop = Math.min(h, Math.max(0, -top));
  const vBot = Math.min(h, Math.max(0, viewH - top));
  if (vBot <= vTop) return 0;   // none of the row is on screen — nothing to anchor to
  // Never take more than half the slice for the band: on a sliver, being INSIDE what
  // the user can see beats clearing a band that is already faded anyway.
  const cut = Math.min(bottom > viewH + 0.5 ? REVEAL_SMOOTH_FEATHER_PX : 0, (vBot - vTop) / 2);
  return h - (vBot - cut);
};

// …and whether it may be shown AT ALL: on a short visible slice the pill overflows onto
// the neighbouring bubble, so a row that cannot hold it cleanly gets none; a fully
// visible row always does. `hidden` gives the threshold hysteresis against flutter. Pure.
export const REVEAL_TRIGGER_SIZE = TUNE.REVEAL_TRIGGER_SIZE;    // .chat-row-menu-btn is 21×21
export const REVEAL_TRIGGER_PAD = TUNE.REVEAL_TRIGGER_PAD;      // clearance from the neighbouring bubble
const REVEAL_TRIGGER_HYSTERESIS = TUNE.REVEAL_TRIGGER_HYSTERESIS;
export const revealTriggerFits = (top, bottom, viewH, hidden = false) => {
  const h = bottom - top;
  if (h <= 0) return false;
  const clipped = top < -0.5 || bottom > viewH + 0.5;
  if (!clipped) return true;
  const vTop = Math.min(h, Math.max(0, -top));
  const vBot = Math.min(h, Math.max(0, viewH - top));
  const cut = Math.min(bottom > viewH + 0.5 ? REVEAL_SMOOTH_FEATHER_PX : 0, (vBot - vTop) / 2);
  const room = REVEAL_TRIGGER_SIZE + REVEAL_TRIGGER_PAD + cut + (hidden ? REVEAL_TRIGGER_HYSTERESIS : 0);
  return vBot - vTop >= room;
};

// Grain for a row. The dot screen covers the whole element, so the clipped share won't
// do: a row taller than the scroller is clipped by definition and would stay speckled.
// Showing as much as the viewport holds counts as fully visible. Pure.
export const revealGrain = (top, bottom, viewH) => {
  const h = bottom - top;
  if (viewH <= 0 || h <= 0) return 0;
  const visible = Math.max(0, Math.min(bottom, viewH) - Math.max(top, 0));
  return 1 - visible / Math.min(h, viewH);
};

// Watch `root` and ramp every child matching `selector` as it scrolls. Returns a
// disconnect function; safe to call in any environment.
// Deliberately LAZY: geometry is measured once per list change (per-frame rect reads
// force layout and stutter) and the mask mounts only on rows straddling an edge.
// `smooth` is for TEXT rows (the chat transcript): the dot grain eats letters on the
// cut edge, so they get one clean alpha fade — and the "…"-fits class.
export function observeReveal(root, selector, { smooth = false } = {}) {
  const noop = () => {};
  if (!root?.addEventListener || typeof requestAnimationFrame === 'undefined') return noop;

  const seen = new WeakSet();
  let rows = [];        // cached geometry: { el, top, h } in the scroller's content space
  let raf = 0;

  // One layout read for the whole list, and only when the list itself changed.
  const measure = () => {
    const rootTop = root.getBoundingClientRect().top - root.scrollTop;
    rows = [...root.querySelectorAll(selector)].map((el) => {
      const r = el.getBoundingClientRect();
      return { el, top: r.top - rootTop, h: r.height };
    });
  };

  const apply = () => {
    raf = 0;
    const viewH = root.clientHeight;
    const scrollTop = root.scrollTop;
    for (const row of rows) {
      const top = row.top - scrollTop;             // pure arithmetic — no layout read
      const bottom = top + row.h;
      const d = revealDissolve(top, bottom, viewH);
      // Only a row straddling an edge needs the mask; everything else is either fully
      // readable or fully clipped away by the scroller.
      const masked = d > 0.001 && d < 0.999;
      if (masked !== row.masked) {
        row.masked = masked;
        row.el.classList.toggle(REVEAL_MASKED_CLASS, masked);
      }
      // Published for EVERY row, masked or not (and before the bailout below): bottom-
      // anchored chrome rides this so a cut row keeps its control against the visible
      // bottom. Written only when it moves — this runs per scroll frame.
      const vb = Math.round(revealVisibleBottom(top, bottom, viewH));
      if (vb !== row.visBottom) {
        row.visBottom = vb;
        row.el.style.setProperty('--visible-bottom', `${vb}px`);
      }
      // …and whether that chrome fits at all, or would be drawn over the neighbour.
      if (smooth) {
        const hide = !revealTriggerFits(top, bottom, viewH, row.noTrigger);
        if (hide !== row.noTrigger) {
          row.noTrigger = hide;
          row.el.classList.toggle(REVEAL_NO_TRIGGER_CLASS, hide);
        }
      }
      if (!masked) continue;
      if (row.h > 0) {
        row.el.style.setProperty('--vis-start', `${(Math.min(1, Math.max(0, -top / row.h)) * 100).toFixed(2)}%`);
        row.el.style.setProperty('--vis-end', `${(Math.min(1, Math.max(0, (viewH - top) / row.h)) * 100).toFixed(2)}%`);
      }
      // …softened only where the scroller really cuts (revealFeather): an edge the user
      // can see in full stays hard, so nothing readable — nor the "…" trigger on the
      // row's bottom edge — is sanded away by a clip happening at the OTHER end.
      const fade = revealFeather(top, bottom, viewH, smooth ? REVEAL_SMOOTH_FEATHER : REVEAL_FEATHER);
      row.el.style.setProperty('--fade-in', fade.in);
      row.el.style.setProperty('--fade-out', fade.out);
      // The soft edge (--vis-*) follows the clipping; the grain has its own ramp so a
      // long message stays readable while you are reading it. Text rows have no grain.
      if (!smooth) row.el.style.setProperty('--dissolve', revealGrain(top, bottom, viewH).toFixed(3));
    }
  };
  const schedule = () => { if (!raf) raf = requestAnimationFrame(apply); };
  const remeasure = () => { measure(); schedule(); };

  // New rows start dissolved and carry a LONGER transition for their first ramp, so an
  // arriving message assembles visibly instead of tracking the scroll instantly.
  const scan = () => {
    for (const el of root.querySelectorAll(selector)) {
      if (seen.has(el)) continue;
      seen.add(el);
      el.classList.add(REVEAL_ITEM_CLASS, REVEAL_MASKED_CLASS, REVEAL_ENTERING_CLASS);
      if (smooth) el.classList.add(REVEAL_SMOOTH_CLASS);
      el.style.setProperty('--dissolve', smooth ? '0' : '1');
      el.style.setProperty('--vis-start', '50%');   // assembles outward from its middle
      el.style.setProperty('--vis-end', '50%');
      setTimeout(() => el.classList.remove(REVEAL_ENTERING_CLASS), REVEAL_ENTER_MS + 60);
    }
    remeasure();
  };
  scan();

  root.addEventListener('scroll', schedule, { passive: true });
  let mo = null;
  if (typeof MutationObserver !== 'undefined') {
    mo = new MutationObserver(scan);
    mo.observe(root, { childList: true, subtree: true });
  }
  let ro = null;
  if (typeof ResizeObserver !== 'undefined') {
    ro = new ResizeObserver(remeasure);   // the scroller resized: every cached top moved
    ro.observe(root);
  }
  return () => {
    root.removeEventListener('scroll', schedule);
    mo?.disconnect();
    ro?.disconnect();
  };
}
