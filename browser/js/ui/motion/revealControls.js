import { motionReduced } from '../motionPrefs.js';
import { MARK_IN_MS, MARK_LEAVING_CLASS, MARK_OUT_MS, markIn, markOut } from './marks.js';
import { groupPainter } from './painters.js';
import { TUNE } from './tune.js';
// A group's clock is longer than a mark's; the dust rides it too, so both land together.
export const REVEAL_GROUP_IN_MS = TUNE.REVEAL_GROUP_IN_MS;
export const REVEAL_GROUP_OUT_MS = TUNE.REVEAL_GROUP_OUT_MS;

// A bar holding revealed controls never flies itself — its controls do — so this is a
// display flip, deferred on the way out by their flight (desktop: updateBatchBar).
// `want()` is asked now and again on arrival, so a selection made mid-flight keeps it.
export const BAR_CLOSING_CLASS = 'bar-closing';
// Worn first: held at its measured height so the controls leaving cannot collapse it early.
export const BAR_HELD_CLASS = 'bar-held';

const releaseBarSlot = (el) => {
  el.classList?.remove(BAR_CLOSING_CLASS, BAR_HELD_CLASS);
  el.style.removeProperty?.('--bar-h');
  el.style.removeProperty?.('--reveal-ms');
};

export const closeBarSlot = (el, ms, { setTimer = setTimeout } = {}) => {
  const h = parseFloat(el.style.getPropertyValue?.('--bar-h'))
    || el.getBoundingClientRect?.().height || 0;
  if (!h || motionReduced()) { releaseBarSlot(el); el.style.display = 'none'; return false; }
  el.style.setProperty('--reveal-ms', `${ms}ms`);
  el.style.setProperty('--bar-h', `${h}px`);
  el.classList.add(BAR_HELD_CLASS);
  void el.offsetWidth;                    // commit the held height as the start
  el.classList.add(BAR_CLOSING_CLASS);    // …and everything it owns goes to zero
  setTimer(() => { el.style.display = 'none'; releaseBarSlot(el); }, ms);
  return true;
};

export const revealBar = (el, want, { display = 'flex', ms = 0, setTimer = setTimeout } = {}) => {
  if (!el?.style) return false;
  const shown = el.style.display !== 'none';
  if (want()) {
    releaseBarSlot(el);   // asked back mid-close: give its own height back first
    if (!shown) el.style.display = display;   // at once: the slot the controls fly INTO
    return !shown;
  }
  if (!shown) return false;
  if (motionReduced()) { el.style.display = 'none'; return false; }
  const out = ms || REVEAL_GROUP_OUT_MS;
  // Freeze the footprint NOW, before the controls inside start leaving.
  const held = el.getBoundingClientRect?.().height || 0;
  if (held) { el.style.setProperty?.('--bar-h', `${held}px`); el.classList?.add(BAR_HELD_CLASS); }
  setTimer(() => {
    if (want()) { releaseBarSlot(el); return; }   // wanted again mid-wait: it stays
    closeBarSlot(el, out, { setTimer });
  }, out);
  return false;
};
const REVEAL_GROUP_TRANSITION_CLASS = 'reveal-group-transition';

// One slide of a revealed group's slot. `defer` waits two painted frames before `to`.
// `ease` is the slot's own curve: the app's usual bezier is half done in 30ms, which makes
// a whole slot jump open and crawl; opening rides easeOutCubic, closing the modal's S.
const REVEAL_EASE_IN = TUNE.REVEAL_EASE_IN;
const REVEAL_EASE_OUT = TUNE.REVEAL_EASE_OUT;
const slideRevealSize = (el, sizeProp, from, to, ms, { defer = false, slack = 0, cleanup = null, ease = REVEAL_EASE_IN } = {}) => {
  el.classList.add(REVEAL_GROUP_TRANSITION_CLASS);
  el.style.setProperty('--reveal-ease', ease);
  el.style[sizeProp] = from;
  void el.offsetWidth;   // commit FROM as the transition's start value
  const go = () => {
    el.style.setProperty('--reveal-ms', `${ms}ms`);
    el.style[sizeProp] = to;
    setTimeout(() => {
      cleanup?.();
      el.classList.remove(REVEAL_GROUP_TRANSITION_CLASS);
      el.style[sizeProp] = '';
      el.style.removeProperty('--reveal-ms');
      el.style.removeProperty('--reveal-ease');
    }, ms + slack);
  };
  if (!defer) { go(); return; }
  const raf = typeof requestAnimationFrame === 'function'
    ? requestAnimationFrame
    : (fn) => setTimeout(fn, 16);
  raf(() => raf(go));
};

// Keep a group's cloud anchored to the group for its flight (desktop:
// DisintegrateOverlay::setFollow): a sibling's slot pushes it along the row meanwhile.
// Left/top only; stops with the host or on display:none.
const followDust = (el, ms) => {
  if (typeof requestAnimationFrame !== 'function' || !el.getBoundingClientRect) return;
  const started = Date.now();
  const step = () => {
    const host = el.__dustHost;
    if (!host || Date.now() - started >= ms) return;
    const r = el.getBoundingClientRect();
    if (!r || (!r.width && !r.height)) return;
    host.style.left = `${r.left}px`;
    host.style.top = `${r.top}px`;
    requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
};

// `dust: false` slides the slot without a cloud (a wide, mostly empty element; desktop
// twin: revealControls' `dust`). `ms` overrides the slot's clock so a group can land
// together with something else.
export function revealControls(el, show, display = 'inline-flex',
                               { vertical: axis = null, dust = true, ms = 0 } = {}) {
  const inMs = ms || REVEAL_GROUP_IN_MS;
  const outMs = ms || REVEAL_GROUP_OUT_MS;
  if (!el?.style) return false;
  const wasShown = el.style.display !== 'none';
  if (wasShown === !!show) return false;   // already there: nothing comes or goes
// A block collapses its height, an inline group its width; a full-width flex bar must
// still open downward, hence the override (connectModal.js's selection bar).
  const vertical = axis === null ? display === 'block' : !!axis;
  const sizeProp = vertical ? 'maxHeight' : 'maxWidth';
  if (show) {
    el.style.display = display;
    const r = el.getBoundingClientRect();   // now laid out at its natural size
    const size = vertical ? r.height : r.width;
    const played = dust ? markIn(el, { ms: inMs, painter: groupPainter(el) }) : !motionReduced();
    if (size && played)
      slideRevealSize(el, sizeProp, '0px', `${size}px`, inMs, { defer: true, slack: 40 });
    if (dust && played) followDust(el, inMs);
    return played;
  }
// Measured while still laid out, so the dust and the collapse start from the true box.
  const r = el.getBoundingClientRect();
  const size = vertical ? r.height : r.width;
  const played = dust
    ? markOut(el, { ms: outMs, painter: groupPainter(el), veil: MARK_LEAVING_CLASS })
    : !motionReduced();
  if (size && played) {
    slideRevealSize(el, sizeProp, `${size}px`, '0px', outMs,
      { ease: REVEAL_EASE_OUT, cleanup: () => { el.style.display = 'none'; } });
  } else {
    el.style.display = 'none';   // declined (reduced motion, too small): instant, as before
  }
  if (dust && played) followDust(el, outMs);
  return played;
}

// Swap a control's text: the mark comes apart and the new one forms out of the motes.
// `apply` runs between the two flights, so the DOM is never behind the state.
export function markSwap(el, apply, { ms = MARK_IN_MS, outMs = MARK_OUT_MS, paint = null } = {}) {
  if (typeof apply !== 'function') return false;
// Unowned: the arrival below is the flight this element owns.
  const left = markOut(el, { ms: outMs, paint, own: false });
  apply();
  return markIn(el, { ms, paint }) || left;
}
