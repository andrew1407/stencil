// ── Once-per-release dispatch gate ──────────────────────────────────────────
// A drag released on a menu item must run its action EXACTLY once. Every re-entry path
// (a click synthesized on top of the drop, a listener seeing the event on its way to the
// document, a re-dispatch in a test harness) lands within a few ms, so one window covers
// them all. Pure — the clock is injected.

// How long after a dispatch any follow-up counts as the same release.
export const DISPATCH_WINDOW_MS = 300;

/**
 * `allow()` is true for the first call and false for anything inside the window;
 * `suppressed()` asks the same without consuming the gate (to swallow a stray click);
 * `reset()` re-arms it. `now` is the clock seam.
 */
export const createOnceGate = ({ windowMs = DISPATCH_WINDOW_MS, now = () => Date.now() } = {}) => {
  let last = null;
  const within = () => last !== null && (now() - last) < windowMs;
  return {
    allow() {
      if (within()) return false;
      last = now();
      return true;
    },
    suppressed: within,
    reset() { last = null; },
  };
};
