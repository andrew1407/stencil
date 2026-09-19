// Shared rig for the motion.test.js family: a rect literal, the class-toggling element
// stand-in, and the document swap the themeSwap specs run inside.

export const box = (left, top, width, height) => ({ left, top, width, height });

// A minimal element stand-in for the class-toggling helpers below.
export const el = (cls = '') => {
  const classes = new Set(cls ? cls.split(' ') : []);
  return {
    offsetWidth: 0,
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
      contains: (c) => classes.has(c),
      toggle: (c, on) => (on ? classes.add(c) : classes.delete(c)),
    },
    has: (c) => classes.has(c),
  };
};

// themeSwap runs `apply` exactly once and synchronously on every path: the palette write
// is the contract, the wipe is decoration on top of it.
export const withDoc = (doc, fn) => {
  const prior = globalThis.document;
  const priorMM = globalThis.matchMedia;
  globalThis.document = doc;
  globalThis.matchMedia = () => ({ matches: false });
  try { return fn(); } finally { globalThis.document = prior; globalThis.matchMedia = priorMM; }
};
export const rootStub = () => {
  const classes = new Set();
  const props = {};
  let sawInstant = false;
  rootStub.lastAnimate = undefined;
  return {
    classList: {
      add: (c) => { classes.add(c); if (c === 'theme-instant') sawInstant = true; },
      remove: (c) => classes.delete(c), contains: (c) => classes.has(c),
    },
    style: { setProperty: (k, v) => { props[k] = v; } },
    get sawInstantDuringApply() { return sawInstant; },
    animate: (...args) => { rootStub.lastAnimate = args; },
    props,
    has: (c) => classes.has(c),
  };
};
