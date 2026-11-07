// The points panel's fold played through its real click, painted frame by frame on a recording
// canvas under a hand-cranked clock, against the desktop's sequence (DockChrome::panelSurfaceFlight):
// under particles the card is never seen sliding, its header and table stay put as the picture,
// and the grains leave it — never a still grid of specks (user report) — clear of the panel box.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { installCoordFoldRig } from '../../helpers/coordFoldRig.js';

const rig = await installCoordFoldRig();
const { doc, fold, css, panel, body, header, shut, HEAD, BODY, frames, lifted, toggle, mean, at, pins, px, rect } = rig;
const { PANEL_DUST_IN_MS, PANEL_DUST_OUT_MS, PANEL_SLIDE_IN_MS, VEILED_CLASS, TALL_CLASS } = fold;
test.mock.timers.enable({ apis: ['setTimeout'] });
const reset = (mode, open) => rig.reset((ms) => test.mock.timers.tick(ms), mode, open);

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

// A fold spans the column to its bottom, as the desktop's dock (animations/collapse.css .coord-tall): a short
// list still breaks over the whole column, while at rest the card keeps the height its rows give it.
test('particles: a short list still breaks over the whole column, and the card goes back to its rows', () => {
  const had = panel.getBoundingClientRect;
  panel.getBoundingClientRect = () => (panel.classes.has(TALL_CLASS) ? rect(888, 70, 324, 800) : rect(888, 70, 324, 140));
  try {
    reset('particles', true);
    toggle();
    assert.ok(panel.classes.has(TALL_CLASS), 'the fold takes the column');
    assert.equal(panel.style['--coord-card-h'], '140px', 'the card drawn at its resting height');
    const host = panel.__dustHost;
    assert.deepEqual([host.style.top, host.style.height], ['70px', '800px'], 'the cloud spans the column, not header and table');
    test.mock.timers.tick(2000);
    assert.ok(!panel.classes.has(TALL_CLASS), 'and at rest the card is its rows again');
  } finally {
    panel.getBoundingClientRect = had;
  }
});

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

// Sliding pictures the whole column as it stands (the card, and the page under it painted in), as the desktop's
// dock pours, taken as the fold starts; on screen it is unpainted again before anything paints.
test('slide: its picture is the whole column as it stands, the card at its own height', async () => {
  const hadRect = panel.getBoundingClientRect;
  const hadImage = globalThis.Image;
  const set = panel.style.setProperty;
  const cardH = [];
  const fill = [];
  panel.getBoundingClientRect = () => (panel.classes.has(TALL_CLASS) ? rect(888, 70, 324, 800) : rect(888, 70, 324, 140));
  panel.style.setProperty = (k, v) => { if (k === '--coord-card-h') cardH.push(v); if (k === '--coord-tall-fill') fill.push(v); return set.call(panel.style, k, v); };
  try {
    globalThis.Image = class {};
    reset('slide', true);
    cardH.length = fill.length = 0;
    toggle();
    assert.deepEqual(cardH, ['140px'], 'the card is never drawn taller than it stands');
    assert.deepEqual(fill, ['var(--bg-container)', 'transparent'], 'the page under it painted in for the picture, then not');
    await new Promise((r) => setImmediate(r));   // the picture's promise settles here, not in the next case
    test.mock.timers.tick(2000);
    globalThis.Image = hadImage;
    reset('particles', true);
    cardH.length = fill.length = 0;
    toggle();
    assert.deepEqual([cardH, fill], [['140px'], ['transparent', 'transparent']], 'the particle modes paint nothing in');
  } finally {
    panel.getBoundingClientRect = hadRect;
    panel.style.setProperty = set;
    globalThis.Image = hadImage;
  }
});

test('slide, none and the OS preference: no veil, no cloud, the CSS slide as before', () => {
  for (const [mode, os] of [['slide', false], ['none', false], ['particles', true]]) {
    reset(mode, true);
    rig.setReduced(os);
    toggle();
    const label = `${mode}${os ? ' + OS' : ''}`;
    assert.ok(shut(), `${label}: still folds`);
    assert.ok(!panel.__dustHost && !panel.classes.has(VEILED_CLASS), `${label}: nothing flies`);
    assert.equal(frames.length, 0);
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
  // The opening picture is taken with transitions off: its chevron already faces the way the panel opens.
  assert.match(css('collapse.css'), /\.coordinates-panel\.fold-instant #toggle-coord-panel \.ic \{ transition: none !important; \}/,
    'the chevron is photographed turned, not mid-spin');
  // As the desktop's: one outlined square folded or not, and a keyboard ring the narrow rail cannot clip.
  const panelCss = readFileSync(new URL('../../../css/layout/coord/panel.css', import.meta.url), 'utf8');
  assert.doesNotMatch(panelCss, /coord-collapsed #toggle-coord-panel \{ border: none/, 'the folded chevron keeps its square');
  assert.match(panelCss, /#toggle-coord-panel:focus-visible \{ outline-offset: -2px; \}/, 'its ring drawn inside its edge');
  const cloud = css('dust.css');
  assert.match(cloud, /\.disintegrate-host\.dust-leaving\.dust-over-picture \{ animation: none; \}/,
    'leaving, a grain shows at full strength as it goes: no ramp over a picture already fading (user report)');
  assert.match(cloud, /@keyframes dustHostSettle \{ 0%, 55% \{ opacity: 1; \} 80%, 100% \{ opacity: 0; \} \}/);
});

