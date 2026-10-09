// The points panel's fold played through its real click, painted frame by frame on a recording
// canvas under a hand-cranked clock, against the desktop's sequence (DockChrome::panelSurfaceFlight):
// under particles the card is never seen sliding, its header and table stay put as the picture,
// and the grains leave it — never a still grid of specks (user report) — clear of the panel box.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { createStubElement } from '../../helpers/dom.js';
import { installDustDom, rect } from '../../helpers/dustRig.js';

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
const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');
const fold = await import('../../../js/ui/panel/coordFold.js');
const { PANEL_DUST_IN_MS, PANEL_DUST_OUT_MS, PANEL_SLIDE_IN_MS, VEILED_CLASS } = fold;
const { StencilMainContent } = await import('../../../js/ui/panel/mainContent.js');
const css = (p) => readFileSync(new URL(`../../../css/animations/${p}`, import.meta.url), 'utf8');

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
const reset = (mode, open) => {
  test.mock.timers.tick(2000);
  if (shut() === open) toggle();
  test.mock.timers.tick(2000);
  dust.reset();
  frames.length = 0;
  lifted.length = 0;
  clock = 0;
  reduced = false;
  setMotionPrefs({ mode });
};
const pins = (el) => ['left', 'top', 'width', 'height'].map((k) => el.style[`--pin-${k}`]);
const px = (r) => [r.left, r.top, r.width, r.height].map((v) => `${v}px`);
test.mock.timers.enable({ apis: ['setTimeout'] });

for (const mode of ['particles', 'water', 'fire']) {
  test(`${mode}: the panel's picture breaks into grains that stream right, its card unseen`, () => {
    reset(mode, true);
    toggle();
    lifted.length = 0;
    assert.ok(shut(), 'the panel folds');
    assert.equal(frames.length, 1, 'the first frame paints on the click');
    assert.equal(frames[0].length, 0, 'no speck sits on the picture before its grain leaves');
    // The card goes at once; header and table hold their open boxes out of the sliding layout.
    assert.ok(panel.classes.has(VEILED_CLASS));
    assert.deepEqual(pins(header), px(HEAD));
    assert.deepEqual(pins(body), px(BODY));
    const host = panel.__dustHost;
    assert.equal(host.parentNode, doc.body, 'the cloud flies on <body>, over the panel, never inside it');
    assert.deepEqual([host.style.left, host.style.top, host.style.height], ['900px', '80px', '500px'],
      'over header and table both');
    const first = Math.min(...host.__cloud.motes.map((m) => m.delay));
    assert.equal(at(first - 1).length, 0, '…right up to the first departure');
    const xs = [100, 140, 180].map((t) => mean(at(first + t)));
    for (let i = 1; i < xs.length; i++) assert.ok(xs[i] > xs[i - 1], `drifting right: ${xs.join(' → ')}`);
    assert.ok(Math.max(...frames.at(-1)) > HEAD.left + HEAD.width, 'and on out past the panel box');
    test.mock.timers.tick(PANEL_DUST_OUT_MS - 1);
    assert.ok(panel.classes.has(VEILED_CLASS), 'veiled while the grains fly');
    test.mock.timers.tick(1);
    assert.deepEqual(lifted, [VEILED_CLASS, 'coord-folding'], 'the veil comes off under the hold');
  });
}

test('opening, the grains gather in from the right over the picture, the card back as the slide lands', () => {
  reset('particles', false);
  toggle();
  lifted.length = 0;
  assert.ok(!shut() && panel.classes.has(VEILED_CLASS));
  const host = panel.__dustHost;
  assert.match(host.className, /dust-over-picture/);
  const first = Math.min(...host.__cloud.motes.map((m) => m.delay));
  assert.equal(at(first - 1).length, 0, 'a gathering grain is nothing until it sets off');
  const xs = [first + 60, first + 160, first + 260].map((t) => mean(at(t)));
  assert.ok(xs[0] > xs[1] && xs[1] > xs[2], `arriving from the right: ${xs.join(' → ')}`);
  // Landed by the end of the flight, as the desktop's `(t - delay) / (1 - delay)`: a late grain
  // flying a whole span of its own was still far off when the cloud faded (user report).
  assert.ok(host.__cloud.motes.every((m) => m.delay + m.dur <= PANEL_DUST_IN_MS), 'every grain lands in time');
  test.mock.timers.tick(PANEL_SLIDE_IN_MS - 1);
  assert.ok(panel.classes.has(VEILED_CLASS));
  test.mock.timers.tick(1);
  assert.deepEqual(lifted, ['coord-folding', VEILED_CLASS], 'the table is never held out as it lands');
});

test('slide, none and the OS preference: no veil, no cloud, the CSS slide as before', () => {
  for (const [mode, os] of [['slide', false], ['none', false], ['particles', true]]) {
    reset(mode, true);
    reduced = os;
    toggle();
    const label = `${mode}${os ? ' + OS' : ''}`;
    assert.ok(shut(), `${label}: still folds`);
    assert.ok(!panel.__dustHost && !panel.classes.has(VEILED_CLASS), `${label}: nothing flies`);
    assert.equal(frames.length, 0);
  }
});

test('stacked under the canvas nothing slides: the table is its own picture, unveiled', () => {
  const wide = globalThis.matchMedia;
  globalThis.matchMedia = (q) => ({ matches: q === '(max-width: 960px)' });
  try {
    reset('particles', true);
    toggle();
    assert.ok(shut() && !panel.classes.has(VEILED_CLASS));
    assert.ok(body.__dustHost?.__cloud.motes.every((m) => m.cut === 1), 'the table\'s grains, cut out of it');
    assert.equal(frames[0].length, 0, 'no speck waits on the table');
  } finally {
    globalThis.matchMedia = wide;
  }
});

// One table for both apps (config/motion.json PANEL_*; the desktop reads it in PanelSlide.hpp):
// the panel slides on PANEL_SLIDE_* and the fold ease in both, veiled or not (user report).
test('the panel clocks are motion.json PANEL_*, handed to its CSS, which names no clock of its own', () => {
  const tune = JSON.parse(readFileSync(new URL('../../../../common/config/motion.json', import.meta.url), 'utf8')).ui;
  const clocks = { PANEL_DUST_IN_MS: 450, PANEL_DUST_OUT_MS: 390, PANEL_SLIDE_IN_MS: 470,
                   PANEL_SLIDE_OUT_MS: 470, PANEL_FADE_MS: 250 };
  for (const [key, ms] of Object.entries(clocks)) {
    assert.equal(tune[key], ms, `motion.json ${key}`);
    assert.equal(fold[key], ms, `coordFold.js ${key} is read from the table`);
  }
  for (const [name, ms] of Object.entries(fold.PANEL_CLOCK_VARS)) {
    assert.equal(panel.style[name], `${ms}ms`, `${name} is set on the panel`);
  }
  const all = css('collapse.css') + readFileSync(new URL('../../../css/layout/coord/panel.css', import.meta.url), 'utf8');
  for (const name of Object.keys(fold.PANEL_CLOCK_VARS)) {
    assert.ok(all.includes(`var(${name})`), `${name} drives a transition`);
    assert.doesNotMatch(all, new RegExp(`${name}\\s*:`), `${name} is never given a value in CSS`);
  }
  const veil = /\.coordinates-panel\.coord-veiled \{([^}]*)\}/.exec(css('collapse.css'))[1];
  assert.match(veil, /background: transparent/);
  assert.doesNotMatch(veil, /transition/, 'the veiled panel keeps the slide its rail takes');
  assert.match(css('collapse.css'), /--fold-ease: cubic-bezier\(0\.39, 0\.575, 0\.565, 1\);/, 'as panelSlideEase');
  const cloud = css('dust.css');
  assert.match(cloud, /\.disintegrate-host\.dust-leaving\.dust-over-picture \{ animation: none; \}/,
    'leaving, a grain shows at full strength as it goes: no ramp over a picture already fading (user report)');
  assert.match(cloud, /@keyframes dustHostSettle \{ 0%, 55% \{ opacity: 1; \} 80%, 100% \{ opacity: 0; \} \}/);
});
