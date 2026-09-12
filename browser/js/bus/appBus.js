// The one home for the stencil:* channels (config/events.json). Delivery IS a window
// event — the contract the extension's content scripts read — and nothing throws, so a
// caller with no DOM (worker, test) is never broken.
import EVENTS from '../config/events.json' with { type: 'json' };

export { EVENTS };

const win = () => (typeof window !== 'undefined' ? window : null);
const doc = () => (typeof document !== 'undefined' ? document : null);

// No `detail` sends a plain Event, as does an engine without CustomEvent.
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

export const subscribe = (channel, handler, { target = win(), once = false } = {}) => {
  if (!target?.addEventListener) return () => {};
  target.addEventListener(channel, handler, once ? { once: true } : undefined);
  return () => target.removeEventListener?.(channel, handler);
};

// `ready` is the one channel on `document`: it fires once, and every <stencil-*>
// element waits for it to be handed the app.
export const publishReady = (app) => publish(EVENTS.ready, { app }, { target: doc() });
export const onReady = (cb) => subscribe(EVENTS.ready, (e) => cb(e.detail.app), { target: doc(), once: true });
