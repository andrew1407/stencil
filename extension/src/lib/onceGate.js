// ── Once-per-release dispatch gate ──────────────────────────────────────────
// A drag released on a menu item must run its action EXACTLY once. The obvious
// re-entry paths (a click synthesized on top of the drop, a listener that also sees
// the event on its way to the document, a re-dispatched event in a test harness) all
// land within a few milliseconds of each other, so one small gate covers them: the
// first call in a window wins and every follow-up inside it is refused.
//
// Pure — the clock is injected, so `node --test` drives it without timers.

// How long after a dispatch any follow-up counts as the same release.
export const DISPATCH_WINDOW_MS = 300;

/**
 * @param {object} [opts]
 * @param {number} [opts.windowMs] - Suppression window (DISPATCH_WINDOW_MS).
 * @param {Function} [opts.now] - Clock seam, ms.
 * @returns {{allow: Function, suppressed: Function, reset: Function}}
 *   `allow()` returns true for the first call and false for anything inside the
 *   window; `suppressed()` answers the same question without consuming the gate
 *   (used to swallow a stray click); `reset()` re-arms it.
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
