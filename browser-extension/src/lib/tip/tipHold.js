// PORT of browser/js/ui/tip/tipHold.js (browser-extension/tests/portParity.test.js): the hold a
// control drag puts on every tooltip. Nothing here drags, so the port's control tip asks and is
// always answered "not held".

const watchers = new Set();
let held = false;

export const tipsHeld = () => held;

// `fn` runs as each hold begins, to drop whatever its tooltip shows; the return unregisters it.
export const onTipsHeld = (fn) => {
  watchers.add(fn);
  return () => watchers.delete(fn);
};

export const holdTips = (on) => {
  const was = held;
  held = !!on;
  if (held && !was) for (const fn of watchers) fn();
};
