// A stub page on which the motion helpers (js/ui/motion.js) really build their clouds: a body
// that keeps every host disintegrate() appends, with its __cloud, class and inline style, and a
// hand-cranked requestAnimationFrame queue. installDustDom() installs the globals for the file.
import { installDom, createStubElement } from './dom.js';

export const rect = (left, top, width, height) =>
  ({ left, top, width, height, right: left + width, bottom: top + height, x: left, y: top });

// `reduced` answers prefers-reduced-motion; `style` overrides what getComputedStyle reports.
export const installDustDom = ({ reduced = () => false, style = {}, docOpts = {}, globals = {} } = {}) => {
  const frames = [];
  const doc = installDom(docOpts, {
    requestAnimationFrame: (fn) => frames.push(fn),
    cancelAnimationFrame: () => {},
    matchMedia: (q) => ({ matches: q.includes('reduced-motion') ? reduced() : false }),
    getComputedStyle: () => ({
      color: 'rgb(20, 20, 20)', backgroundColor: 'rgb(240, 240, 240)', borderTopStyle: 'none',
      borderTopWidth: '0px', borderTopColor: 'rgb(0, 0, 0)', getPropertyValue: () => '', ...style,
    }),
    ...globals,
  });
  return {
    doc,
    frames,
    // Every cloud layer on <body>, oldest first; `reset()` forgets them between cases.
    clouds: () => doc.body.children.filter((h) => h?.__cloud),
    reset: () => { doc.body.children.length = 0; frames.length = 0; },
    // Run the frames queued so far (not the ones they queue in turn).
    frame: () => { const run = frames.splice(0); run.forEach((fn) => fn()); return run.length; },
  };
};

// A laid-out element whose box is `r` (a function, so a case can move it mid-flight).
export const boxEl = (r, overrides = {}) =>
  createStubElement('div', { getBoundingClientRect: typeof r === 'function' ? r : () => r, ...overrides });

// The cloud an element owns, by its flight class ('dust-forming', 'dust-leaving', …), or null.
export const cloudKind = (el) => el.__dustHost?.className.replace('disintegrate-host ', '') ?? null;
export const near = (a, b, tol = 20) => Math.abs(a.x - b.x) < tol && Math.abs(a.y - b.y) < tol;

// Where the motes of a surface cloud converge on average: each flies home → home + (dx, dy).
export const cloudAim = (host) => {
  const { motes } = host.__cloud;
  const sum = motes.reduce((a, m) => ({ x: a.x + m.x + m.dx, y: a.y + m.y + m.dy }), { x: 0, y: 0 });
  return { x: sum.x / motes.length, y: sum.y / motes.length };
};
