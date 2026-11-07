// The open panel's entrance is a per-mode CSS animation (animations/overlay/overlays.css): a mode picked while it
// is up swapped that name and replayed it, as if it reopened (user report). It is settled instead, until it closes;
// a close still plays its own exit.
import { subscribe, EVENTS } from '../../../eventBus/appBus.js';

export const MOTION_SETTLED_CLASS = 'chat-motion-settled';

// Returns the teardown that undoes both the subscription and the watch.
export function settleOnMotionChange(host) {
  if (!host?.classList) return () => {};
  const open = () => host.classList.contains('chat-open') && !host.classList.contains('chat-closing');
  const unsubscribe = subscribe(EVENTS.motionChanged, () => { if (open()) host.classList.add(MOTION_SETTLED_CLASS); });
  const watch = typeof MutationObserver === 'function'
    // Only when it is there: a remove() rewrites the class attribute even when nothing changes.
    ? new MutationObserver(() => {
      if (host.classList.contains(MOTION_SETTLED_CLASS) && !host.classList.contains('chat-open')) host.classList.remove(MOTION_SETTLED_CLASS);
    })
    : null;
  watch?.observe(host, { attributes: true, attributeFilter: ['class'] });
  return () => { unsubscribe?.(); watch?.disconnect(); };
}
