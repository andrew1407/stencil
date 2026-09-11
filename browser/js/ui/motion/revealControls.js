import { motionReduced } from '../motionPrefs.js';
import { MARK_IN_MS, MARK_LEAVING_CLASS, MARK_OUT_MS, markIn, markOut } from './marks.js';
import { groupPainter } from './painters.js';
import { TUNE } from './tune.js';
// clock, so it gets its own longer one — handed to the dust too, so the two land together.
export const REVEAL_GROUP_IN_MS = TUNE.REVEAL_GROUP_IN_MS;
export const REVEAL_GROUP_OUT_MS = TUNE.REVEAL_GROUP_OUT_MS;

// A BAR holding revealed controls (the selection strips): the BAR ITSELF never flies —
// only its controls do, so this is a display flip, deferred on the way OUT by their
// flight (the desktop's ProjectsDialog / ConnectDialog::updateBatchBar). Sliding its own
// slot was tried and read wrong both ways: opening it clipped the button forming inside
// it, closing it left the border and padding as a bare grey line (user report).
// `want()` is the single source of whether the bar belongs — asked now, and again on
// arrival, so a selection made mid-flight keeps it. Timer injectable — unit-tested.
// The class a leaving bar wears while its SLOT closes: height, padding and the divider
// under it all go together, or whatever is left of its footprint drops the list below it
// in one frame at the end.
export const BAR_CLOSING_CLASS = 'bar-closing';
// …and the freeze it wears first. Its controls fly out before the slot closes, and the
// last one being hidden takes the strip's content height with it — that collapse IS the
// jump, before any slide could start (user report: the pinned row jumped when Select all
// left). Held at its measured height, the strip keeps its shape while they leave.
export const BAR_HELD_CLASS = 'bar-held';

const releaseBarSlot = (el) => {
  el.classList?.remove(BAR_CLOSING_CLASS, BAR_HELD_CLASS);
  el.style.removeProperty?.('--bar-h');
  el.style.removeProperty?.('--reveal-ms');
};

// Close the bar's slot from the height it is holding, then take it out of the flow.
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
// A transition, not @keyframes: markIn/markOut may also add `.mark-forming`
// (an animation), and two `animation` rules on one element would fight over a winner.
const REVEAL_GROUP_TRANSITION_CLASS = 'reveal-group-transition';

// One slide of the space a revealed group reserves: commit `from` as the transition's
// start, apply `to`, clean up after `ms` (+`slack`). `defer` waits two painted frames
// before `to` — set in the same busy turn, the box leapt to wherever the curve already was.
// `ease` is the slot's own curve: the app's usual cubic-bezier(0.16, 1, .3, 1) is half done
// in 30ms, which makes a whole group's slot jump open and then crawl. Opening rides
// easeOutCubic, closing the gentle S the modal flight closes on.
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

// Keep a group's cloud anchored to the group for the length of its flight — the desktop's
// DisintegrateOverlay::setFollow. A control revealed beside a sibling is photographed
// where it sits, and the sibling's slot then pushes it along the row: the motes gathered
// where the group first stood and jumped over on landing (user report). Left/top only —
// the box they fly at is the natural one while the slot itself is mid-slide. Stops with
// the host, or on display:none (an all-zero rect).
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

// `dust: false` slides the slot without a cloud — for a wide, mostly EMPTY element,
// whose motes are a grey band the width of the window rather than anything the eye can
// follow (user report). Its CONTENTS still dust. Desktop twin: revealControls' `dust`.
// `ms` overrides the slot's own clock, for a group that must land together with something
// else — the connections bar leaves beside the row that emptied it.
export function revealControls(el, show, display = 'inline-flex',
                               { vertical: axis = null, dust = true, ms = 0 } = {}) {
  const inMs = ms || REVEAL_GROUP_IN_MS;
  const outMs = ms || REVEAL_GROUP_OUT_MS;
  if (!el?.style) return false;
  const wasShown = el.style.display !== 'none';
  if (wasShown === !!show) return false;   // already there: nothing comes or goes
  // Which way the slot closes: a block collapses its height, an inline group its width.
  // Right for every caller but a full-width bar, which is a flex row that must still open
  // downward — hence the explicit override (connectModal.js's selection bar).
  const vertical = axis === null ? display === 'block' : !!axis;
  const sizeProp = vertical ? 'maxHeight' : 'maxWidth';
  if (show) {
    el.style.display = display;
    const r = el.getBoundingClientRect();   // now laid out at its natural size
    const size = vertical ? r.height : r.width;
    // grid sized off that same natural box, painted in the group's own colours
    const played = dust ? markIn(el, { ms: inMs, painter: groupPainter(el) }) : !motionReduced();
    if (size && played)
      slideRevealSize(el, sizeProp, '0px', `${size}px`, inMs, { defer: true, slack: 40 });
    if (dust && played) followDust(el, inMs);
    return played;
  }
  // Measured while it is still laid out, so both the dust and the collapse start
  // from the true box.
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

// Swap the TEXT a control displays, the mark coming apart and the new one forming out
// of the motes. `apply` writes the new value; it runs between the two flights, so the
// element is never blank and the DOM is never behind the state.
export function markSwap(el, apply, { ms = MARK_IN_MS, outMs = MARK_OUT_MS, paint = null } = {}) {
  if (typeof apply !== 'function') return false;
  // The outgoing cloud is deliberately UNOWNED: the arrival below is the flight this
  // element owns, and claiming both would have the second cancel the first.
  const left = markOut(el, { ms: outMs, paint, own: false });
  apply();
  return markIn(el, { ms, paint }) || left;
}
