// A drag released on a menu item must run its action exactly once: every re-entry path (a
// synthesized click, a re-dispatch) lands within a few ms, so one window covers them all.

// How long after a dispatch a follow-up still counts as the same release.
export const DISPATCH_WINDOW_MS = 300;

// `suppressed()` asks without consuming the gate.
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
