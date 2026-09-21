// themeSwap (js/ui/motion.js): the palette write happens once on every path, the wake is
// spawned in the OLD palette, and the circle rides CSS custom properties.
import test from 'node:test';
import assert from 'node:assert';
import {
  themeSwap, originOf, THEME_SWAP_MS, THEME_SWAP_CLASS, SWAP_DUST_LIFE_MS, SWAP_EDGE_POINTS,
  DUST_ALPHA_LEVELS,
} from '../js/ui/motion.js';
import { FILL_CHUNK } from '../js/ui/dust/dustCloud.js';
import { ANIMATIONS_CSS } from './helpers/css.js';
import { el, withDoc, rootStub } from './helpers/motionRig.js';

test('themeSwap spawns the wake once the transition is ready, in the OLD palette', async () => {
  const root = rootStub();
  const bodyChildren = [];
  // A canvas that records what was actually painted: the colour of each fill, its alpha,
  // and how many arcs rode in it.
  const mkCanvas = () => {
    const fills = [];
    const el = {
      className: '', width: 0, height: 0, style: {}, removed: false,
      remove() { el.removed = true; },
    };
    let arcs = 0;
    const ctx = {
      fillStyle: '#000', globalAlpha: 1, cleared: 0,
      scale() {}, clearRect() { ctx.cleared++; }, beginPath() { arcs = 0; },
      moveTo() {}, arc() { arcs++; },
      fill() { fills.push({ colour: ctx.fillStyle, alpha: ctx.globalAlpha, arcs }); },
    };
    el.getContext = () => ctx;
    el.fills = fills;
    return el;
  };
  const doc = {
    documentElement: root,
    // The palette probe spans (dustCloud.js resolveColour) come and go on <body> too.
    createElement: (tag) => (tag === 'canvas' ? mkCanvas() : {
      style: { setProperty() {} }, appendChild() {},
      remove() { const i = bodyChildren.indexOf(this); if (i >= 0) bodyChildren.splice(i, 1); },
    }),
    body: { appendChild: (el2) => bodyChildren.push(el2) },
    startViewTransition: (cb) => { cb(); return { ready: Promise.resolve(), finished: Promise.resolve() }; },
  };
  const priorWin = globalThis.window;
  const priorCS = globalThis.getComputedStyle;
  const priorDoc = globalThis.document;
  const priorMM = globalThis.matchMedia;
  const priorRaf = globalThis.requestAnimationFrame;
  const priorCancel = globalThis.cancelAnimationFrame;
  globalThis.window = { innerWidth: 300, innerHeight: 400, devicePixelRatio: 2 };
  // The vars as they read BEFORE apply — the wake must bake these, not the new theme's.
  globalThis.getComputedStyle = () => ({
    getPropertyValue: (n) => ({ '--bg-page': '#111318', '--text-main': '#e8eaf0', '--accent': '#eab308' }[n] || ''),
  });
  // Installed for the WHOLE test, not via withDoc: the wake spawns from ready's
  // microtask, after a withDoc would already have restored the globals.
  globalThis.document = doc;
  globalThis.matchMedia = () => ({ matches: false });
  let pending = null;
  globalThis.requestAnimationFrame = (cb) => { pending = cb; return 1; };
  globalThis.cancelAnimationFrame = () => { pending = null; };
  try {
    themeSwap(() => {}, { x: 150, y: 200 });
    await Promise.resolve();   // let `ready` deliver
    await Promise.resolve();
    assert.equal(bodyChildren.length, 1, 'one dust layer on <body>');
    const stage = bodyChildren[0];
    assert.equal(stage.className, 'swap-dust');
    // Sized in device pixels, laid out in CSS ones — a hi-dpi wake is not a blurry one.
    assert.deepEqual([stage.width, stage.height], [600, 800]);
    assert.deepEqual([stage.style.width, stage.style.height], ['300px', '400px']);

    // Mid-wake grains come from the palette resolved BEFORE the flip, so the stub yields
    // color-mix() strings.
    const t0 = performance.now();
    pending(t0 + THEME_SWAP_MS / 2);
    const lit = stage.fills.filter((f) => f.arcs > 0);
    assert.ok(lit.reduce((n, f) => n + f.arcs, 0) > 30, 'a field of grains');
    assert.ok(lit.some((f) => f.colour === 'color-mix(in srgb, var(--accent) 100%, var(--accent-2))'), 'accent grains');
    assert.ok(lit.some((f) => f.colour === 'color-mix(in srgb, var(--accent) 0%, var(--accent-2))'), 'shade grains');
    // The whole point of the stage: batched fills — one per (stop, alpha step), each in
    // chunks of FILL_CHUNK grains (dustCloud.js) — not one per grain.
    const grains = lit.reduce((n, f) => n + f.arcs, 0);
    assert.ok(stage.fills.length <= 6 * DUST_ALPHA_LEVELS + Math.ceil(grains / FILL_CHUNK),
      `batched into ${stage.fills.length} fills, not ${grains}`);
    assert.ok(lit.every((f) => f.arcs <= FILL_CHUNK), 'no path longer than a chunk');
    assert.ok(lit.every((f) => f.alpha > 0 && f.alpha <= 1), 'every batch carries its own alpha');

    // …and it reaps itself once the last grain has burnt out.
    pending(t0 + THEME_SWAP_MS + SWAP_DUST_LIFE_MS + 1);
    assert.ok(stage.removed, 'the stage clears itself off the page');
  } finally {
    clearTimeout(root._swapDustTimer);   // reap the layer's own timer — tests must not linger
    globalThis.window = priorWin;
    globalThis.getComputedStyle = priorCS;
    globalThis.document = priorDoc;
    globalThis.matchMedia = priorMM;
    globalThis.requestAnimationFrame = priorRaf;
    globalThis.cancelAnimationFrame = priorCancel;
  }
});

test('themeSwap without View Transitions transitions the palette and still applies it', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const root = rootStub();
  let applied = 0;
  withDoc({ documentElement: root }, () => themeSwap(() => { applied++; }));
  assert.equal(applied, 1, 'the palette write happens exactly once');
  assert.ok(root.has(THEME_SWAP_CLASS), 'colour consumers get their one beat of transition');
  t.mock.timers.tick(THEME_SWAP_MS + 1);
  assert.ok(!root.has(THEME_SWAP_CLASS), 'and the class is cleaned up after');
});

test('themeSwap hands the circle to CSS as custom properties, not a scripted animation', async () => {
  // A view transition ends once its pseudo-elements have no animations left, so the keyframes
  // live in CSS and this supplies only the origin and radius.
  const root = rootStub();
  let applied = 0;
  const doc = { documentElement: root,
    startViewTransition: (cb) => { cb(); return { ready: Promise.resolve(), finished: Promise.resolve() }; } };
  const priorWin = globalThis.window;
  globalThis.window = { innerWidth: 300, innerHeight: 400 };
  try {
    withDoc(doc, () => themeSwap(() => { applied++; }, { x: 0, y: 0 }));
  } finally { globalThis.window = priorWin; }
  assert.equal(applied, 1, 'the palette write happens exactly once');
  assert.equal(root.props['--swap-x'], '0%');
  assert.equal(root.props['--swap-y'], '0%');
  // 500px of a 300x400 viewport, as the percentage clip-path resolves against sqrt(w²+h²)/√2.
  assert.equal(root.props['--swap-r'], '141.421%', 'the radius reaches the furthest corner');
  assert.equal(root.props['--swap-ms'], `${THEME_SWAP_MS}ms`);
  // …and the clip the reveal actually plays: the ragged polygon pair, equal vertex
  // counts so the two interpolate.
  const count = (p) => (String(p).match(/%/g) || []).length / 2;
  assert.ok(String(root.props['--swap-clip-from']).startsWith('polygon('), 'a collapsed ragged start');
  assert.ok(String(root.props['--swap-clip-to']).startsWith('polygon('), 'a full ragged ring');
  assert.equal(count(root.props['--swap-clip-from']), SWAP_EDGE_POINTS);
  assert.equal(count(root.props['--swap-clip-to']), SWAP_EDGE_POINTS);
  assert.equal(rootStub.lastAnimate, undefined, 'nothing is animated from script');
  // Transitions are suppressed WHILE the snapshot is captured, or it records the old
  // colours mid-ease and the wipe reveals a half-changed page.
  assert.ok(root.sawInstantDuringApply, 'colour transitions are off while the new state is captured');
});

test('themeSwap still applies the palette when the document is a bare stub', () => {
  let applied = 0;
  withDoc({ documentElement: {} }, () => themeSwap(() => { applied++; }));
  assert.equal(applied, 1, 'decoration is optional; the write never is');
});

test('originOf resolves a control to its centre, and declines an unrendered one', () => {
  assert.deepEqual(originOf({ getBoundingClientRect: () => ({ left: 10, top: 20, width: 40, height: 10 }) }),
    { x: 30, y: 25 });
  assert.equal(originOf({ getBoundingClientRect: () => ({ left: 0, top: 0, width: 0, height: 0 }) }), null);
  assert.equal(originOf(null), null);
});

test('animations.css: the swap wipe is declarative, and the fallback transitions colours', () => {
  const css = ANIMATIONS_CSS;
  // Both default cross-fades are off — motion.js drives the clip itself.
  // Declarative, so the transition waits for it instead of tearing down mid-wipe.
  assert.ok(/::view-transition-old\(root\) \{ z-index: 0; animation: none; \}/.test(css),
    'the OLD snapshot keeps no default animation');
  assert.ok(/animation: themeSwapReveal var\(--swap-ms/.test(css), 'the reveal is a CSS animation');
  // The front is the ragged polygon pair motion.js supplies; the circle is only the
  // fallback for a write that never happened.
  assert.ok(/@keyframes themeSwapReveal \{[\s\S]*?clip-path: var\(--swap-clip-from, circle\(0%/.test(css),
    'collapsing from the ragged start, circle as fallback');
  assert.ok(/@keyframes themeSwapReveal \{[\s\S]*?clip-path: var\(--swap-clip-to, circle\(var\(--swap-r/.test(css),
    'growing to the ragged ring motion.js supplies');
  const fallback = css.slice(css.indexOf('html.theme-swapping'));
  assert.ok(/background-color 0\.45s/.test(fallback) && /color 0\.45s/.test(fallback)
    && /border-color 0\.45s/.test(fallback), 'every colour consumer eases, not just the body');
});
