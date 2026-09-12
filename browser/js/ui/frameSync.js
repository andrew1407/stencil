// The shared resize listener and a per-frame coalescer: a handler that measures or paints
// must not run more often than the screen refreshes.
const subscribers = new Set();
let frame = 0;
let wired = null;      // the window we are attached to (identity, not a flag — tests swap it)

const runAll = () => {
  frame = 0;
  for (const fn of [...subscribers]) {
    try { fn(); } catch { /* one subscriber's failure must not strand the others */ }
  }
};

// One passive resize listener for every subscriber, run together in one frame in
// subscription order. (dropdownMenu.js keeps its own — byte-pinned to the extension's copy.)
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

// A burst of calls runs `fn` once on the next frame with the newest arguments; without rAF
// (node) it calls straight through.
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
