// A scroller and its rows for observeReveal (js/ui/motion/reveal.js): boxes in viewport space,
// a hand-cranked requestAnimationFrame queue, and every CSS var write a row receives, in order.
// A row's box is fixed at mount: observeReveal measures once and follows scrollTop by arithmetic.

export const revealRow = (top, height) => {
  const classes = new Set();
  const writes = [];
  return {
    classes, writes,
    classList: {
      add: (...cs) => cs.forEach((c) => classes.add(c)),
      remove: (...cs) => cs.forEach((c) => classes.delete(c)),
      toggle: (c, on) => (on ? classes.add(c) : classes.delete(c)),
      contains: (c) => classes.has(c),
    },
    style: { setProperty: (k, v) => writes.push([k, v]) },
    prop: (k) => writes.filter(([w]) => w === k).at(-1)?.[1],
    getBoundingClientRect: () => ({ top, height }),
  };
};

// `rows` are [top, height] pairs; installs requestAnimationFrame until `restore()`.
export const revealScroller = (rows, viewH = 500) => {
  const frames = [];
  globalThis.requestAnimationFrame = (fn) => { frames.push(fn); return frames.length; };
  const els = rows.map(([top, h]) => revealRow(top, h));
  const listeners = {};
  const root = {
    scrollTop: 0, clientHeight: viewH,
    getBoundingClientRect: () => ({ top: 0 }),
    querySelectorAll: () => els,
    addEventListener: (t, fn) => { (listeners[t] ||= []).push(fn); },
    removeEventListener() {},
  };
  const frame = () => frames.splice(0).forEach((fn) => fn());
  const restore = () => { delete globalThis.requestAnimationFrame; };
  return { root, els, listeners, frame, restore };
};
