// A control group's reveal: its slot slides open or shut on its own clock while the group's mark comes apart
// into motes, slides whole (Sliding) or grows (Transition); a bar holding such groups and a text swap ride it.
// Desktop twin: support/control/reveal/ (revealControls, edgeFlight, modeGhost).
import { dustEnabled, motionMode, motionMs, motionReduced } from '../motionPrefs.js';
import { MARK_IN_MS, MARK_LEAVING_CLASS, MARK_OUT_MS, markIn, markOut } from '../surface/marks.js';
import { groupPainter } from '../surface/painters.js';
import { SURFACE_FORMING_CLASS } from '../surface/motion.js';
import { TUNE } from '../tune.js';
import { slideGhost, settleSlideGhost } from './modeGhost.js';
// A group's clock is longer than a mark's; the dust rides it too, so both land together.
export const REVEAL_GROUP_IN_MS = TUNE.REVEAL_GROUP_IN_MS;
export const REVEAL_GROUP_OUT_MS = TUNE.REVEAL_GROUP_OUT_MS;

// A bar holding revealed controls never flies itself — its controls do — so this is a display
// flip, deferred on the way out by their flight (desktop: updateBatchBar).
export const BAR_CLOSING_CLASS = 'bar-closing';
// Worn first: held at its measured height so the controls leaving cannot collapse it early.
export const BAR_HELD_CLASS = 'bar-held';

const barRuns = new WeakMap();     // bar → its latest close run; an older close no longer hides it
const settles = new WeakMap();     // group → the settle of its slide in flight
const rowHolds = new WeakMap();    // wrapping row → how many reveals hold its height
const leaving = new WeakSet();     // groups whose leave is still sliding

const releaseBarSlot = (el) => {
  barRuns.set(el, (barRuns.get(el) ?? 0) + 1);
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
  const run = (barRuns.get(el) ?? 0) + 1;
  barRuns.set(el, run);
  setTimer(() => { if (barRuns.get(el) === run) { el.style.display = 'none'; releaseBarSlot(el); } }, ms);
  return true;
};

export const revealBar = (el, want, { display = 'flex', ms = 0, setTimer = setTimeout } = {}) => {
  if (!el?.style) return false;
  const isShown = el.style.display !== 'none';
  if (want()) {
    releaseBarSlot(el);   // asked back mid-close: give its own height back first
    if (!isShown) el.style.display = display;   // at once: the slot the controls fly INTO
    return !isShown;
  }
  if (!isShown) return false;
  if (motionReduced()) { el.style.display = 'none'; return false; }
  const out = ms || REVEAL_GROUP_OUT_MS;
  // No grains fly in it (Sliding, Transition): it closes with its controls, as the desktop's does (user report).
  if (!dustEnabled()) { closeBarSlot(el, out, { setTimer }); return false; }
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

// `ease` is the slot's own curve: the app's usual bezier is half done in 30ms, which makes a
// slot jump open and crawl — opening rides easeOutCubic, closing the modal's S.
const REVEAL_EASE_IN = TUNE.REVEAL_EASE_IN;
const REVEAL_EASE_OUT = TUNE.REVEAL_EASE_OUT;
const slideRevealSize = (el, sizeProp, from, to, ms, { defer = false, slack = 0, cleanup = null, ease = REVEAL_EASE_IN } = {}) => {
  el.classList.add(REVEAL_GROUP_TRANSITION_CLASS);
  el.style.setProperty('--reveal-ease', ease);
  el.style[sizeProp] = from;
  void el.offsetWidth;   // commit FROM as the transition's start value
  const go = () => {
    el.style.setProperty('--reveal-ms', `${motionMs(ms)}ms`);
    el.style[sizeProp] = to;
    let timer = 0;
    // Idempotent, and kept on the element: a reveal arriving mid-leave lands the leave first (revealControls).
    const settle = () => {
      clearTimeout(timer);
      if (settles.get(el) !== settle) return;
      settles.delete(el);
      cleanup?.();
      el.classList.remove(REVEAL_GROUP_TRANSITION_CLASS);
      el.style[sizeProp] = '';
      el.style.removeProperty('--reveal-ms');
      el.style.removeProperty('--reveal-ease');
    };
    settles.set(el, settle);
    timer = setTimeout(settle, motionMs(ms + slack));
  };
  if (!defer) { go(); return; }
  const raf = typeof requestAnimationFrame === 'function'
    ? requestAnimationFrame
    : (fn) => setTimeout(fn, 16);
  raf(() => raf(go));
};

// Sliding and Transition have no cloud over the slot: the group's picture slides whole along its axis
// (modeGhost.js), or the group grows (css/animations/reveal/groups.css revealGrow), on the slot's own clock.
const modeTimers = new WeakMap();   // element → the timer that ends its grow
const noFx = () => () => {};
const slideFx = (el, { dir, vertical, ms, r }) => { slideGhost(el, r, { opening: dir === 'in', vertical, ms }); return () => {}; };
const growFx = (el, { dir, ms }) => {
  if (!el.classList) return () => {};
  clearTimeout(modeTimers.get(el));   // a leave inside the grow's window keeps its own clock
  const cls = `reveal-mode-${dir}`;
  el.style.setProperty('--reveal-mode-ms', `${motionMs(ms)}ms`);
  el.classList.remove('reveal-mode-in', 'reveal-mode-out');
  el.classList.add(cls);
  const off = () => { el.classList.remove(cls); el.style.removeProperty('--reveal-mode-ms'); };
  if (dir === 'in') modeTimers.set(el, setTimeout(off, motionMs(ms) + 40));
  return off;
};
const MODE_FX = Object.freeze({ slide: slideFx, transition: growFx });
const modeFx = (el, fx) => (MODE_FX[motionMode()] ?? noFx)(el, fx);

// The outermost wrapping flex row around `el`: a group sliding open inside it re-wraps the rows partway,
// and everything below (the canvas an arrival had just measured) jumped a row (user report).
const wrapRow = (el) => {
  if (typeof getComputedStyle !== 'function') return null;
  let row = null;
  for (let p = el.parentElement; p && p !== document.body; p = p.parentElement)
    if (getComputedStyle(p).flexWrap === 'wrap') row = p;
  return row;
};
// The groups of this sweep whose slide has not started (no --reveal-ms yet) sat at their 0px start and wrapped the
// row short, so it grew a line once they stood (user report): the row is measured with each at its natural width.
const pendingGroups = (node, out = []) => {
  for (const c of node.children ?? []) {
    if (c.classList?.contains(REVEAL_GROUP_TRANSITION_CLASS) && !c.style?.getPropertyValue?.('--reveal-ms')) out.push(c);
    pendingGroups(c, out);
  }
  return out;
};
const settledRowHeight = (row) => {
  if (!row?.getBoundingClientRect) return 0;
  const pending = pendingGroups(row);
  const was = pending.map((g) => [g.style.maxWidth, g.style.maxHeight]);
  for (const g of pending) { g.style.maxWidth = 'none'; g.style.maxHeight = 'none'; }
  const h = row.getBoundingClientRect().height;
  pending.forEach((g, i) => { [g.style.maxWidth, g.style.maxHeight] = was[i]; });
  return h;
};
// Held at least `h` px tall for `ms`, the height an opening ends at; never closing, where the row may be a bar
// that closes with the group (held, it stood full height and vanished, user report). Counted, released once.
const holdRow = (row, h, ms) => {
  if (!row?.style || !(h > 0)) return;
  rowHolds.set(row, (rowHolds.get(row) ?? 0) + 1);
  row.style.minHeight = `${Math.max(h, parseFloat(row.style.minHeight) || 0)}px`;
  setTimeout(() => {
    const held = rowHolds.get(row) - 1;
    rowHolds.set(row, held);
    if (held === 0) row.style.minHeight = '';
  }, ms);
};

// `dust: false` slides the slot without a cloud (desktop twin: revealControls' `dust`); `ms` overrides the slot's
// clock so a group can land with something else. In `slide` the slot alone is the flight; only motionReduced() skips it.
export function revealControls(el, show, display = 'inline-flex',
                               { vertical: axis = null, dust = true, ms = 0 } = {}) {
  const inMs = ms || REVEAL_GROUP_IN_MS;
  const outMs = ms || REVEAL_GROUP_OUT_MS;
  if (!el?.style) return false;
  // Asked back while still leaving (a search typed on): the leave lands at once and the group comes back,
  // where the leave used to finish over the reveal and hide it for good (user report).
  if (show && leaving.has(el)) { settleSlideGhost(el); settles.get(el)?.(); }
  const wasShown = el.style.display !== 'none';
  if (wasShown === !!show) return false;   // already there: nothing comes or goes
  // A block collapses its height, an inline group its width; a full-width flex bar must
  // still open downward, hence the override (modal.js's selection bar).
  const vertical = axis === null ? display === 'block' : !!axis;
  const sizeProp = vertical ? 'maxHeight' : 'maxWidth';
  if (show) {
    el.style.display = display;
    const r = el.getBoundingClientRect();   // now laid out at its natural size
    const size = vertical ? r.height : r.width;
    // Unrendered, or inside a window still forming from dust: the window's own flight brings it.
    if ((!r.width && !r.height) || el.closest?.(`.${SURFACE_FORMING_CLASS}`)) return false;
    const isDusted = dust && markIn(el, { ms: inMs, painter: groupPainter(el) });
    const wasPlayed = isDusted || !motionReduced();
    if (size && wasPlayed) {
      const row = vertical ? null : wrapRow(el);   // read at the natural size: the height the rows end at
      holdRow(row, settledRowHeight(row), motionMs(inMs + 40) + 50);
      if (!isDusted) modeFx(el, { dir: 'in', vertical, ms: inMs, r });
      slideRevealSize(el, sizeProp, '0px', `${size}px`, inMs, { defer: true, slack: 40 });
    }
    return wasPlayed;
  }
  // Measured while still laid out, so the dust and the collapse start from the true box.
  const r = el.getBoundingClientRect();
  const size = vertical ? r.height : r.width;
  const isDusted = dust && markOut(el, { ms: outMs, painter: groupPainter(el), veil: MARK_LEAVING_CLASS });
  const wasPlayed = isDusted || !motionReduced();
  if (size && wasPlayed) {
    const off = isDusted ? () => {} : modeFx(el, { dir: 'out', vertical, ms: outMs, r });
    leaving.add(el);
    slideRevealSize(el, sizeProp, `${size}px`, '0px', outMs,
      { ease: REVEAL_EASE_OUT, cleanup: () => { leaving.delete(el); el.style.display = 'none'; off(); } });
  } else {
    el.style.display = 'none';   // declined (reduced motion, too small): instant, as before
  }
  return wasPlayed;
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
