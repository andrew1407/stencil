import { motionReduced } from '../motionPrefs.js';

// The desktop dialog's own height ease (openImageDialogParts.hpp OI_RESIZE_MS).
export const BOX_RESIZE_MS = 380;

// A box sized by its content SNAPS the moment that content changes; this eases it there
// instead, its own natural height still deciding where it lands. Restarted rather than
// queued, so a change arriving mid-flight is chased (desktop: OpenImageDialog::animateHeightTo).
// Only the ROWS are watched, never the box or the scroller: a flight owns the box's height,
// and measuring anything that follows it would feed the next target back from the last one.
// Returns a stop function.
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
    // The layout is ALREADY at `want` and a WAAPI flight's first frame is the NEXT one, so
    // the start is pinned here: unpinned, the box paints one frame at the target and the
    // flight rewinds — a snap followed by a slide.
    box.style.height = `${from}px`;
    const f = box.animate([{ height: `${from}px` }, { height: `${want}px` }],
                          { duration: ms, easing: 'cubic-bezier(0.22,0.61,0.36,1)' });
    flight = f;
    f.finished.then(() => { if (flight === f) { flight = null; release(); } }, () => {});
  });
  for (const row of scroller.children) ro.observe(row);
  return () => { ro.disconnect(); flight?.cancel(); flight = null; release(); };
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
