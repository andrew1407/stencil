import { motionReduced } from '../motionPrefs.js';
import { TUNE } from './tune.js';
// transform-origin for a fixed menu popping out of its open point: the click clamped
// into the placed box, relative to its top-left.
export const menuPopOrigin = (x, y, rect) => {
  const cl = (v, max) => Math.min(Math.max(v, 0), max);
  return `${cl(x - rect.left, rect.width)}px ${cl(y - rect.top, rect.height)}px`;
};

// FLIP: play an element's NEW box out of the one it had. Call AFTER the layout change with
// the rect measured BEFORE it. Transform-only, so it never re-triggers layout.
export const FLIP_MS = TUNE.FLIP_MS;
export const FLIP_EASING = TUNE.FLIP_EASING;
// Held for the whole flight; CSS lifts the element above the page and stops its scrollbars
// flashing while scaled.
export const FLIP_ACTIVE_CLASS = 'flip-active';

// The inverse transform mapping `to` back onto `from`; null when degenerate or already matching.
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
// Measured with the class ON: it changes overflow, and so the box to invert.
  el.classList?.add(activeClass);
  const invert = flipTransform(from, el.getBoundingClientRect());
  if (!invert) { clear(); return; }
  el.style.transformOrigin = 'top left';
  el.style.willChange = 'transform';
  el.style.transition = 'none';
  el.style.transform = invert;
// Two frames: both in one frame would be coalesced into no animation.
  requestAnimationFrame(() => requestAnimationFrame(() => {
    el.style.transition = `transform ${ms}ms ${FLIP_EASING}`;
    el.style.transform = 'none';
  }));
  el._flipTimer = setTimeout(clear, ms + 90);
}
