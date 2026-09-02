// ── Where the user last acted ───────────────────────────────────────────────
// Most surfaces fly out of a control: an icon, a trigger, the click a context menu grew
// from. A CONFIRM dialog has none — it is raised by whatever the user just did, from a
// dozen different handlers, and none of them knows it is about to ask a question. The
// honest origin is that gesture's own point, so it is recorded here once for everyone.
// Keyboard-raised questions fall back to whatever holds focus.
//
// Deliberately NOT in ui/motion.js: that module remembers no pointer state at all, and
// the reason is a scar — a remembered press once played the theme wipe from a corner
// the user had not clicked (tests/motion.test.js pins it). A press is the right origin
// only for a surface RAISED by that press, which is exactly what this module is for.

let point = null;
let at = 0;
// How long a press stays "the last thing the user did". Past it the gesture was a key
// press, and what holds focus is the better answer — a stale click from a minute ago
// would fly the question out of wherever the pointer happened to be resting.
export const GESTURE_FRESH_MS = 1500;
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

// A small client rect around that point, in the shape a modal flight wants for its
// anchor. Null when nothing has been pointed at and nothing holds focus.
export const gestureAnchorRect = () => {
  if (point && Date.now() - at < GESTURE_FRESH_MS) return boxAt(point);
  const a = typeof document !== 'undefined' ? document.activeElement : null;
  const r = a && a !== document.body ? a.getBoundingClientRect?.() : null;
  if (r && r.width > 0 && r.height > 0) return r;
  return point ? boxAt(point) : null;
};
