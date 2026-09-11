// ── Frame-level scheduling: the shared resize listener + a per-frame coalescer ──
// Both exist for the same reason: a handler that measures or paints must not run more
// often than the screen refreshes.
const subscribers = new Set();
let frame = 0;
let wired = null;      // the window we are attached to (identity, not a flag — tests swap it)

const runAll = () => {
  frame = 0;
  for (const fn of [...subscribers]) {
    try { fn(); } catch { /* one subscriber's failure must not strand the others */ }
  }
};

// Subscribe `fn` to window resizes; returns an unsubscribe. Eight modules each measured on
// their own 'resize' handler, so one drag of the window edge ran eight independent layout
// passes per event. They share this one passive listener and run together in ONE frame, in
// subscription order. (ui/dropdownMenu.js keeps its own — it is byte-pinned to the
// extension's copy and can only change in lockstep with it.)
export const onWindowResize = (fn) => {
  subscribers.add(fn);
  if (typeof window !== 'undefined' && window.addEventListener && wired !== window) {
    wired = window;
    window.addEventListener('resize', () => {
      if (frame) return;
      if (typeof requestAnimationFrame === 'function') frame = requestAnimationFrame(runAll);
      else runAll();
    }, { passive: true });
  }
  return () => subscribers.delete(fn);
};

// Wrap `fn` so a burst of calls runs it ONCE on the next frame, with the newest arguments —
// for input that fires faster than the screen refreshes (mousemove). Without rAF (node) it
// calls straight through, so tests see the same synchronous handler they always did.
export const perFrame = (fn) => {
  let pending = null;
  let id = 0;
  return (...args) => {
    if (typeof requestAnimationFrame !== 'function') { fn(...args); return; }
    pending = args;
    if (id) return;
    id = requestAnimationFrame(() => { id = 0; const a = pending; pending = null; fn(...a); });
  };
};
