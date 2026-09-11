// ── App bus: the one home for the stencil:* channels ────────────
// Every announce/listen goes through here, so config/events.json has a single caller and
// no module hand-rolls a dispatch. Delivery IS a window event: that is the contract the
// extension's content scripts read, so the two cross-surface channels (switchToSource,
// registryChanged) still speak it raw at their bridge boundary and everything else here.
// Nothing throws — a nudge to the UI must never break a caller with no DOM (worker, test).
import EVENTS from '../config/events.json' with { type: 'json' };

export { EVENTS };

const win = () => (typeof window !== 'undefined' ? window : null);
const doc = () => (typeof document !== 'undefined' ? document : null);

// Announce a channel. No `detail` sends a plain Event, as does an engine without CustomEvent.
export const publish = (channel, detail = undefined, { target = win() } = {}) => {
  if (!target?.dispatchEvent) return false;
  try {
    target.dispatchEvent(detail === undefined || typeof CustomEvent !== 'function'
      ? new Event(channel)
      : new CustomEvent(channel, { detail }));
    return true;
  } catch {
    return false;
  }
};

// Listen on a channel; returns an unsubscribe.
export const subscribe = (channel, handler, { target = win(), once = false } = {}) => {
  if (!target?.addEventListener) return () => {};
  target.addEventListener(channel, handler, once ? { once: true } : undefined);
  return () => target.removeEventListener?.(channel, handler);
};

// `ready` is the one channel on `document`: it fires once, before any window listener the
// components install, and every <stencil-*> element waits for it to be handed the app.
export const publishReady = (app) => publish(EVENTS.ready, { app }, { target: doc() });
export const onReady = (cb) => subscribe(EVENTS.ready, (e) => cb(e.detail.app), { target: doc(), once: true });
