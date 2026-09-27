import { motionReduced } from './motionPrefs.js';

// The desktop dialog's own height ease (openImageDialogParts.hpp OI_RESIZE_MS).
export const BOX_RESIZE_MS = 380;
export const BOX_RESIZE_EASE = 'cubic-bezier(0.22,0.61,0.36,1)';

// Restarted rather than queued, so a change arriving mid-flight is chased (desktop:
// OpenImageDialog::animateHeightTo). Only the ROWS are watched: the box's height is the flight's.
export const easeBoxHeight = (box, scroller, ms = BOX_RESIZE_MS) => {
  if (!box || !scroller || typeof ResizeObserver === 'undefined' || !box.animate) return () => {};
  let shown = null, flight = null;
  const release = () => box.style.removeProperty('height');
  // Only a scripted flight (cropDims.animate) counts — a button's own hover CSSTransition
  // (controls.css) must not, or it blocks the ease on every tab switch.
  const isScriptedFlight = (a) => a.constructor === Animation;
  const ro = new ResizeObserver(() => {
    if (Array.from(scroller.children)
            .some((row) => row.getAnimations({ subtree: true }).some(isScriptedFlight))) {
      if (!flight) shown = box.offsetHeight;
      return;
    }
    const from = flight ? box.offsetHeight : shown;
    flight?.cancel();
    flight = null;
    release();   // hand the height back to the layout, which has already settled, and read it
    const want = box.offsetHeight;
    shown = want;
    if (from === null || from === want || !want || motionReduced()) return;
    // The layout is ALREADY at `want` and a WAAPI flight's first frame is the NEXT one, so the
    // start is pinned here — unpinned, the box paints one frame at the target and rewinds.
    box.style.height = `${from}px`;
    const f = box.animate([{ height: `${from}px` }, { height: `${want}px` }],
                          { duration: ms, easing: BOX_RESIZE_EASE });
    flight = f;
    f.finished.then(() => { if (flight === f) { flight = null; release(); } }, () => {});
  });
  for (const row of scroller.children) ro.observe(row);
  return () => { ro.disconnect(); flight?.cancel(); flight = null; release(); };
};

// A list held at `held` px through a removal (it sizes its window) lets go by easing down to what it
// now needs, the window following, not dropping there in one frame (user report). Returns a cancel.
export const releaseHeldHeight = (el, held, ms = BOX_RESIZE_MS) => {
  el.style.minHeight = '';
  const want = el.getBoundingClientRect().height;
  if (!(held > want) || motionReduced() || typeof el.animate !== 'function') return () => {};
  el.style.minHeight = `${held}px`;   // the flight's first frame is the next one: pinned till then
  const f = el.animate([{ minHeight: `${held}px` }, { minHeight: `${want}px` }],
                       { duration: ms, easing: BOX_RESIZE_EASE, fill: 'forwards' });
  const done = () => { f.cancel(); el.style.minHeight = ''; };
  f.finished.then(done, () => {});
  return done;
};

// The modal-shell wiring both windows share: start after the entrance (a box eased on its
// own arrival fights it), stop on close. Hand `start`/`stop` to onOpen/onClose.
export const modalBoxEase = (overlay) => {
  let stop = null;
  return {
    start: () => requestAnimationFrame(() => {
      stop?.();
      stop = easeBoxHeight(overlay.querySelector('.app-modal'),
                           overlay.querySelector('.settings-body'));
    }),
    stop: () => { stop?.(); stop = null; },
  };
};
