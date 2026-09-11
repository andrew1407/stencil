import { motionReduced } from '../motionPrefs.js';
import { TUNE } from './tune.js';
// ── Menu pop (canvas context menu + chat row menu) ──────────────────────────
// transform-origin for a fixed-position menu popping out of its OPEN point: the
// click clamped into the menu's placed box (post viewport clamping), relative to
// its top-left — so the pop visibly grows out of the cursor. Pure.
export const menuPopOrigin = (x, y, rect) => {
  const cl = (v, max) => Math.min(Math.max(v, 0), max);
  return `${cl(x - rect.left, rect.width)}px ${cl(y - rect.top, rect.height)}px`;
};

// ── FLIP: play an element's NEW box out of the one it had a moment ago ──────
// Used by the fullscreen toggle: entering stretches out of the old box, leaving
// minimises back into the new one. Call AFTER the layout change, with the rect
// measured BEFORE it. Transform-only, so it never re-triggers layout.
// Long and hard-eased-out, so a full-window stretch reads as deliberate.
export const FLIP_MS = TUNE.FLIP_MS;
export const FLIP_EASING = TUNE.FLIP_EASING;
// Held on the element for the whole flight. CSS uses it to lift the element above the
// page and stop its scrollbars flashing while it is scaled — without it, an element
// scaled UP out of its in-flow box paints behind the toolbars around it.
export const FLIP_ACTIVE_CLASS = 'flip-active';

// The inverse transform mapping `to` back onto `from`, or null when either box is
// degenerate or the two already match (nothing to play). Pure — unit-tested.
export function flipTransform(from, to) {
  if (!from || !to) return null;
  if (!(from.width > 0 && from.height > 0 && to.width > 0 && to.height > 0)) return null;
  const sx = from.width / to.width;
  const sy = from.height / to.height;
  const dx = from.left - to.left;
  const dy = from.top - to.top;
  const still = Math.abs(sx - 1) < 0.001 && Math.abs(sy - 1) < 0.001
    && Math.abs(dx) < 0.5 && Math.abs(dy) < 0.5;
  if (still) return null;
  return `translate(${dx}px, ${dy}px) scale(${sx}, ${sy})`;
}

export function flipFrom(el, from, { ms = FLIP_MS, activeClass = FLIP_ACTIVE_CLASS } = {}) {
  if (!el?.style || typeof requestAnimationFrame === 'undefined') return;
  if (motionReduced()) return;
  const clear = () => {
    el.classList?.remove(activeClass);
    el.style.transition = '';
    el.style.transform = '';
    el.style.transformOrigin = '';
    el.style.willChange = '';
  };
  clearTimeout(el._flipTimer);
  clear();
  // Measured with the class ON: it changes overflow (and so whether scrollbars take
  // space), and a box measured without it would be the wrong one to invert.
  el.classList?.add(activeClass);
  const invert = flipTransform(from, el.getBoundingClientRect());
  if (!invert) { clear(); return; }
  el.style.transformOrigin = 'top left';
  el.style.willChange = 'transform';
  el.style.transition = 'none';
  el.style.transform = invert;
  // Two frames: one lands the inverted state, the next starts the transition off it
  // (setting both in the same frame would be coalesced into no animation at all).
  requestAnimationFrame(() => requestAnimationFrame(() => {
    el.style.transition = `transform ${ms}ms ${FLIP_EASING}`;
    el.style.transform = 'none';
  }));
  el._flipTimer = setTimeout(clear, ms + 90);
}
