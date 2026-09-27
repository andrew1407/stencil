// A stub page the motion helpers (js/ui/motion.js) really fly their clouds on: a <body> that
// keeps every host disintegrate() appends, a hand-cranked requestAnimationFrame queue, and every
// setTimeout held back for the test to fire. installDustPage(t) installs it for one test.
import { installDom, createStubElement } from './dom.js';

export const rect = (left, top, width, height) =>
  ({ left, top, width, height, right: left + width, bottom: top + height });

export const installDustPage = (t, globals = {}) => {
  let seq = 0;
  const frames = new Map();
  const timers = new Map();
  const doc = installDom({}, {
    requestAnimationFrame: (fn) => { frames.set(++seq, fn); return seq; },
    cancelAnimationFrame: (id) => frames.delete(id),
    setTimeout: (fn, ms) => { timers.set(++seq, { fn, ms }); return seq; },
    clearTimeout: (id) => timers.delete(id),
    matchMedia: () => ({ matches: false }),
    ...globals,
  });
  t.after(() => doc.restore());
  return {
    doc,
    clouds: () => doc.body.children.filter((h) => h?.__cloud),
    // Runs the frames queued so far, not the ones they queue in turn.
    frame: () => { const run = [...frames.values()]; frames.clear(); run.forEach((fn) => fn()); return run.length; },
    pendingFrames: () => frames.size,
    delays: () => [...timers.values()].map((x) => x.ms),
    fire: (ms) => {
      for (const [id, x] of [...timers]) if (x.ms === ms) { timers.delete(id); x.fn(); }
    },
    // An element laid out at `box()` (read per call, so it can move) inside a scroller.
    entry: (box, scroller, overrides = {}) => {
      const parent = createStubElement('div', { getBoundingClientRect: () => scroller });
      const el = createStubElement('div', { getBoundingClientRect: () => box(), ...overrides });
      parent.appendChild(el);
      return el;
    },
  };
};

// Where a surface cloud's motes converge on average: each flies home → home + (dx, dy).
export const cloudAim = (host) => {
  const { motes } = host.__cloud;
  const sum = motes.reduce((a, m) => ({ x: a.x + m.x + m.dx, y: a.y + m.y + m.dy }), { x: 0, y: 0 });
  return { x: sum.x / motes.length, y: sum.y / motes.length };
};
