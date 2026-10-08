// While a control is being dragged (ui/drag/iconDrag.js holds it from start to end) no tooltip
// shows: the one up goes as the hold begins and none comes until it ends. Both tooltips — the
// control tip (controlTooltip.js) and the canvas readout (tooltip.js) — ask before they show.
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
