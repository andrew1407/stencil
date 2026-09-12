import { TUNE } from './tune.js';
// Rows dissolve only by the amount the scroller already clips them.
export const REVEAL_ITEM_CLASS = 'reveal-item';
export const REVEAL_IN_CLASS = 'reveal-in';
export const REVEAL_ENTERING_CLASS = 'reveal-entering';
export const REVEAL_MASKED_CLASS = 'reveal-masked';
export const REVEAL_SMOOTH_CLASS = 'reveal-smooth';
export const REVEAL_NO_TRIGGER_CLASS = 'reveal-no-trigger';
export const REVEAL_ENTER_MS = TUNE.REVEAL_ENTER_MS;
// A share of the ROW, matching animations/reveal.css.
export const REVEAL_FEATHER = TUNE.REVEAL_FEATHER;

export const revealDissolve = (top, bottom, viewH) => {
  const h = bottom - top;
  if (viewH <= 0 || h <= 0) return 0;
  const visible = Math.max(0, Math.min(bottom, viewH) - Math.max(top, 0));
  return 1 - visible / h;
};

// Only the edges the scroller really cuts may soften (half-pixel slack: flush is not clipped).
export const revealFeather = (top, bottom, viewH, feather = REVEAL_FEATHER) => ({
  in: top < -0.5 ? feather : '0%',
  out: bottom > viewH + 0.5 ? feather : '0%',
});

// Text rows fade over a FIXED band; the bottom-anchored trigger below clears the same one.
export const REVEAL_SMOOTH_FEATHER_PX = TUNE.REVEAL_SMOOTH_FEATHER_PX;
export const REVEAL_SMOOTH_FEATHER = `${REVEAL_SMOOTH_FEATHER_PX}px`;

// Distance up from the row's bottom where bottom-anchored chrome (the chat "…" trigger)
// must sit to stay inside the visible slice, clear of the fade band.
export const revealVisibleBottom = (top, bottom, viewH) => {
  const h = bottom - top;
  if (h <= 0) return 0;
  const vTop = Math.min(h, Math.max(0, -top));
  const vBot = Math.min(h, Math.max(0, viewH - top));
  if (vBot <= vTop) return 0;   // none of the row is on screen — nothing to anchor to
// Never more than half the slice for the band.
  const cut = Math.min(bottom > viewH + 0.5 ? REVEAL_SMOOTH_FEATHER_PX : 0, (vBot - vTop) / 2);
  return h - (vBot - cut);
};

// Whether the trigger may show at all; `hidden` gives the threshold hysteresis.
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

// The dot screen covers the whole element, so a row taller than the scroller counts as
// fully visible once it shows as much as the viewport holds.
export const revealGrain = (top, bottom, viewH) => {
  const h = bottom - top;
  if (viewH <= 0 || h <= 0) return 0;
  const visible = Math.max(0, Math.min(bottom, viewH) - Math.max(top, 0));
  return 1 - visible / Math.min(h, viewH);
};

// Ramp every child matching `selector` as `root` scrolls; returns a disconnect. Geometry is
// measured once per list change and the mask mounts only on rows straddling an edge.
// `smooth` (text rows) is one clean alpha fade plus the "…"-fits class.
export function observeReveal(root, selector, { smooth = false } = {}) {
  const noop = () => {};
  if (!root?.addEventListener || typeof requestAnimationFrame === 'undefined') return noop;

  const seen = new WeakSet();
  let rows = [];        // cached geometry: { el, top, h } in the scroller's content space
  let raf = 0;

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
      const masked = d > 0.001 && d < 0.999;
      if (masked !== row.masked) {
        row.masked = masked;
        row.el.classList.toggle(REVEAL_MASKED_CLASS, masked);
      }
// Published for every row, before the bailout: bottom-anchored chrome rides this.
      const vb = Math.round(revealVisibleBottom(top, bottom, viewH));
      if (vb !== row.visBottom) {
        row.visBottom = vb;
        row.el.style.setProperty('--visible-bottom', `${vb}px`);
      }
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
      const fade = revealFeather(top, bottom, viewH, smooth ? REVEAL_SMOOTH_FEATHER : REVEAL_FEATHER);
      row.el.style.setProperty('--fade-in', fade.in);
      row.el.style.setProperty('--fade-out', fade.out);
// The grain has its own ramp; text rows have none.
      if (!smooth) row.el.style.setProperty('--dissolve', revealGrain(top, bottom, viewH).toFixed(3));
    }
  };
  const schedule = () => { if (!raf) raf = requestAnimationFrame(apply); };
  const remeasure = () => { measure(); schedule(); };

// New rows start dissolved with a longer first ramp, so an arriving message assembles.
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
