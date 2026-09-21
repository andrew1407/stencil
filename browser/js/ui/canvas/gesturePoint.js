// Where the user last acted. A confirm dialog is raised by whatever the user just did, from
// handlers that don't know they are about to ask, so the gesture's own point is recorded
// here once for everyone. Not in ui/motion.js: a press is the right origin only for a
// surface raised by that press (tests/motion.test.js pins that motion.js keeps no pointer state).

let point = null;
let at = 0;
// Past this a key press is the gesture, and what holds focus is the better answer.
const GESTURE_FRESH_MS = 1500;
export const GESTURE_ANCHOR_PX = 26;

if (typeof document !== 'undefined' && document.addEventListener)
  document.addEventListener('pointerdown', (e) => {
    if (!Number.isFinite(e?.clientX) || !Number.isFinite(e?.clientY)) return;
    point = { x: e.clientX, y: e.clientY };
    at = Date.now();
  }, true);

const boxAt = (p) => {
  const h = GESTURE_ANCHOR_PX / 2;
  return { left: p.x - h, top: p.y - h, right: p.x + h, bottom: p.y + h,
           width: GESTURE_ANCHOR_PX, height: GESTURE_ANCHOR_PX };
};

// A small client rect around that point, shaped for a modal flight's anchor. Null when
// nothing has been pointed at and nothing holds focus.
export const gestureAnchorRect = () => {
  if (point && Date.now() - at < GESTURE_FRESH_MS) return boxAt(point);
  const a = typeof document !== 'undefined' ? document.activeElement : null;
  const r = a && a !== document.body ? a.getBoundingClientRect?.() : null;
  if (r && r.width > 0 && r.height > 0) return r;
  return point ? boxAt(point) : null;
};
