import { motionMs, motionReduced } from './motionPrefs.js';
import { TUNE } from './tune.js';

const flipTimers = new WeakMap();     // el → the timer that clears its FLIP
const glideTimers = new WeakMap();    // el → the timer that clears its glide
const glideSpacers = new WeakMap();   // el → the spacer holding its slot while it glides

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
  const isStill = Math.abs(sx - 1) < 0.001 && Math.abs(sy - 1) < 0.001
    && Math.abs(dx) < 0.5 && Math.abs(dy) < 0.5;
  if (isStill) return null;
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
  clearTimeout(flipTimers.get(el));
  clear();
  // Measured with the class ON: it changes overflow, and so the box to invert.
  el.classList?.add(activeClass);
  const invert = flipTransform(from, el.getBoundingClientRect());
  if (!invert) { clear(); return; }
  el.style.transformOrigin = 'top left';
  el.style.willChange = 'transform';
  el.style.transition = 'none';
  el.style.transform = invert;
  const dur = motionMs(ms);
  // Two frames: both in one frame would be coalesced into no animation.
  requestAnimationFrame(() => requestAnimationFrame(() => {
    el.style.transition = `transform ${dur}ms ${FLIP_EASING}`;
    el.style.transform = 'none';
  }));
  flipTimers.set(el, setTimeout(clear, dur + 90));
}

const GLIDE_PROPS = Object.freeze(['position', 'inset', 'left', 'top', 'width', 'height', 'margin', 'max-width', 'max-height', 'transition']);

// Glide a box between client rects by its real geometry, held fixed for the flight: its content keeps
// its proportions and centring, where a FLIP's uneven scale squashed it. Call AFTER the layout change.
export function glideFrom(el, from, { ms = FLIP_MS, activeClass = FLIP_ACTIVE_CLASS } = {}) {
  if (!el?.style?.setProperty || typeof requestAnimationFrame === 'undefined') return;
  clearTimeout(glideTimers.get(el));
  glideSpacers.get(el)?.remove();
  const clear = () => {
    for (const p of GLIDE_PROPS) el.style.removeProperty(p);
    el.classList?.remove(activeClass);
    glideSpacers.get(el)?.remove();
    glideSpacers.delete(el);
  };
  clear();
  if (motionReduced()) return;
  el.classList?.add(activeClass);
  const to = el.getBoundingClientRect();
  if (!flipTransform(from, to)) { clear(); return; }
  const cs = getComputedStyle(el);
  // An in-flow box keeps its slot while it flies, or what follows it would jump up and back.
  if (cs.position !== 'fixed' && cs.position !== 'absolute') {
    const spacer = document.createElement('div');
    spacer.style.cssText = `flex:none;width:${to.width}px;height:${to.height}px;margin:${cs.margin}`;
    el.before(spacer);
    glideSpacers.set(el, spacer);
  }
  const put = (prop, v) => el.style.setProperty(prop, v, 'important');
  const place = (r) => { put('left', `${r.left}px`); put('top', `${r.top}px`); put('width', `${r.width}px`); put('height', `${r.height}px`); };
  put('position', 'fixed');
  put('inset', 'auto');
  put('margin', '0');
  put('max-width', 'none');
  put('max-height', 'none');
  put('transition', 'none');
  place(from);
  const dur = motionMs(ms);
  // Two frames: both in one frame would be coalesced into no animation.
  requestAnimationFrame(() => requestAnimationFrame(() => {
    put('transition', ['left', 'top', 'width', 'height'].map((p) => `${p} ${dur}ms ${FLIP_EASING}`).join(', '));
    place(to);
  }));
  glideTimers.set(el, setTimeout(clear, dur + 90));
}
