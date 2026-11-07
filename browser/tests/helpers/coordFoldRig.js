// The points panel's fold under test (tests/ui/panel/coordFold*.test.js): the real mainContent click wiring over
// a recording canvas and a hand-cranked clock, the dust DOM, and the panel, header and body stubs. One rig
// for the wide and the stacked suites; `at(t)` paints a frame, `reset(mode, open)` puts the panel back.
import { readFileSync } from 'node:fs';
import { createStubElement } from './dom.js';
import { installDustDom, rect } from './dustRig.js';

export const installCoordFoldRig = async () => {
  let clock = 0;
  let reduced = false;
  const frames = [];   // per painted frame: the x each grain's outline starts at
  const canvasEl = () => {
    const ctx = new Proxy({}, {
      get: (t, k) => (k in t ? t[k] : (...a) => {
        if (k === 'clearRect') frames.push([]);
        else if (k === 'moveTo') frames.at(-1)?.push(a[0]);
      }),
      set: (t, k, v) => { t[k] = v; return true; },
    });
    return createStubElement('canvas', { getContext: () => ctx });
  };
  globalThis.MutationObserver = class { observe() {} disconnect() {} };
  globalThis.customElements = { define: () => {}, get: () => undefined };
  globalThis.HTMLElement = class {};
  const dust = installDustDom({
    reduced: () => reduced,
    docOpts: { autoCreateById: true, createElement: (tag) => (tag === 'canvas' ? canvasEl() : createStubElement(tag)) },
    globals: { performance: { now: () => clock },
               window: { addEventListener: () => {}, removeEventListener: () => {}, dispatchEvent: () => {} } },
  });
  const { doc } = dust;
  doc.els.set('panel-resizer', null);
  const { setMotionPrefs } = await import('../../js/ui/motion/motionPrefs.js');
  const fold = await import('../../js/ui/panel/coordFold.js');
  const { StencilMainContent } = await import('../../js/ui/panel/mainContent.js');
  const css = (p) => readFileSync(new URL(`../../css/animations/${p}`, import.meta.url), 'utf8');

  const panel = doc.register('coord-panel', createStubElement('div'));
  const shut = () => panel.classes.has('coord-collapsed');
  const HEAD = rect(900, 80, 300, 28);
  const BODY = rect(900, 116, 300, 464);
  doc.register('coord-panel-header', createStubElement('div', { getBoundingClientRect: () => HEAD }));
  doc.getElementById('toggle-coord-panel').parentElement = doc.getElementById('coord-panel-header');
  const body = doc.register('coord-body', createStubElement('div', {
    getBoundingClientRect: () => (shut() ? rect(900, 116, 0, 0) : BODY),
  }));
  const header = doc.getElementById('coord-panel-header');
  const lifted = [];   // the order the fold's classes come off at its end
  const remove = panel.classList.remove;
  panel.classList.remove = (...cs) => { lifted.push(...cs); return remove(...cs); };
  StencilMainContent.prototype.wire.call(createStubElement('stencil-main-content'), null);
  const toggle = () => doc.getElementById('toggle-coord-panel').dispatch('click');
  const mean = (xs) => xs.reduce((a, b) => a + b, 0) / xs.length;
  const at = (t) => { clock = t; dust.frame(); return frames.at(-1); };
  // `tick` is the suite's mock-timer advance (node:test's mock is per file).
  const reset = (tick, mode, open) => {
    tick(2000);
    if (shut() === open) toggle();
    tick(2000);
    dust.reset();
    frames.length = 0;
    lifted.length = 0;
    clock = 0;
    reduced = false;
    setMotionPrefs({ mode });
  };
  const pins = (el) => ['left', 'top', 'width', 'height'].map((k) => el.style[`--pin-${k}`]);
  const px = (r) => [r.left, r.top, r.width, r.height].map((v) => `${v}px`);
  const setReduced = (v) => { reduced = v; };
  return { doc, dust, fold, css, setMotionPrefs, panel, body, header, shut, HEAD, BODY, frames, lifted,
           toggle, mean, at, reset, pins, px, setReduced, rect };
};
