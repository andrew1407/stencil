// The palette-swap wipe src/lib/accent.js plays: where the circle blooms from, the polygon pair
// it clips with, and the dust layer seeded in its wake once the transition is ready.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { loadAccent } from '../../helpers/accentSandbox.js';

// The palette-swap wipe starts at the CONTROL that changed the theme — the popup header's round
// toggle — the way the desktop app blooms from its theme button.
const TOGGLE = { id: 'theme-toggle', rect: { left: 276, top: 26, width: 28, height: 28 } };
const wipePage = (opts = {}) => loadAccent({ withViewTransitions: true, controls: [TOGGLE], ...opts });

test('the wipe starts at the theme button, and no press is remembered to override it', () => {
  const page = wipePage();
  assert.equal(page.pointerListeners.length, 0, 'the last click is never the origin');
  page.theme.set('dark');
  assert.deepEqual(page.swapOrigin(), { x: 290, y: 40 }, 'the centre of #theme-toggle');
  // And the circle still has to reach the furthest corner from there.
  assert.equal(Math.round(page.swapRadius()), Math.round(Math.hypot(290, 660)));
  // The reveal's clip is the polygon pair (browser motion.js swapEdgePolygon): equal vertex counts
  // so the two end states interpolate, and the full ring clears the furthest corner.
  const parse = (poly) => [...poly.matchAll(/([\d.-]+)% ([\d.-]+)%/g)]
    .map((m) => ({ x: (parseFloat(m[1]) / 100) * 480, y: (parseFloat(m[2]) / 100) * 700 }));
  const from = parse(page.swapClip('from'));
  const to = parse(page.swapClip('to'));
  assert.equal(from.length, 240);
  assert.equal(to.length, 240);
  assert.ok(from.every((p) => Math.hypot(p.x - 290, p.y - 40) < 0.5), 'collapsed at the origin');
  const radii = to.map((p) => Math.hypot(p.x - 290, p.y - 40));
  const R = Math.hypot(290, 660);
  assert.ok(radii.every((r) => r >= R - 0.05), 'every vertex clears the furthest corner');
  assert.ok(Math.max(...radii) - Math.min(...radii) < 0.1, 'dust: a perfect circle');
});

test('water and fire cut their own front, and the classic-script twins match cloud.js', async () => {
  const dc = await import('../../../src/lib/dust/cloud.js');
  const page = loadAccent();
  const parse = (poly) => [...poly.matchAll(/([\d.-]+)% ([\d.-]+)%/g)]
    .map((m) => Math.hypot((parseFloat(m[1]) / 100) * 480 - 290, (parseFloat(m[2]) / 100) * 700 - 40));
  const R = Math.hypot(290, 660);
  for (const style of [dc.STYLE_WATER, dc.STYLE_FIRE]) {
    const radii = parse(page.motion.edgePolygon(290, 40, 480, 700, 1, style));
    assert.equal(radii.length, 240);
    assert.ok(radii.every((r) => r >= R - 0.05), 'coverage still rules');
    assert.ok(Math.max(...radii) - Math.min(...radii) > R * 0.03, `style ${style}: visibly not a circle`);
    for (const k of [0, 17, 101, 239])
      assert.ok(Math.abs(page.motion.edgeJitter(style, k) - dc.edgeJitter(style, k)) < 1e-12, `edgeJitter ${style}/${k}`);
    for (const w of [0.1, 0.37, 0.8, 0.95]) assert.equal(page.motion.grainShape(style, w), dc.grainShape(style, w));
  }
  assert.equal(page.motion.grainShape(0, 0.8), dc.SHAPE_DISC);
  const norm = (a) => Array.from(a, (v) => +(+v).toFixed(9) + 0);
  assert.deepEqual(norm(page.motion.shapePolygon(dc.SHAPE_TRIANGLE, 10, 20, 2, 0.5)),
                   norm(dc.shapePolygon(dc.SHAPE_TRIANGLE, 10, 20, 2, 0.5)));
  assert.deepEqual(norm(page.motion.shapePolygon(dc.SHAPE_WAVE, 3, 4, 1.5, 2)), norm(dc.shapePolygon(dc.SHAPE_WAVE, 3, 4, 1.5, 2)));
  assert.deepEqual(norm(page.motion.shapePolygon(dc.SHAPE_STREAK, 3, 4, 1.5, 2)), norm(dc.shapePolygon(dc.SHAPE_STREAK, 3, 4, 1.5, 2)));
  // …and so does the tint a grain's hash gives it, and the palette that names it.
  assert.deepEqual([...page.motion.paletteCss()], dc.paletteCss());
  for (let w = 0; w < 1; w += 0.0037) {
    assert.equal(page.motion.tintOf(w), dc.tintOf(w), `tintOf ${w}`);
    assert.equal(page.motion.stopOfTint(0.5, dc.tintOf(w)), dc.stopOfTint(0.5, dc.tintOf(w)), `stopOfTint ${w}`);
  }
});

test('with nothing to anchor to, the wipe blooms from the centre — never a click', () => {
  const page = wipePage({ controls: [] });   // a page with no #theme-toggle at all
  page.theme.set('dark');
  assert.deepEqual(page.swapOrigin(), { x: 240, y: 350 }, 'the viewport centre');
});

test('the caller can hand over the control it owns (the options page has no toggle)', () => {
  const page = wipePage({ controls: [] });
  const select = { getBoundingClientRect: () => ({ left: 100, top: 200, width: 200, height: 30 }) };
  page.theme.set('dark', select);
  assert.deepEqual(page.swapOrigin(), { x: 200, y: 215 });
  const trigger = { getBoundingClientRect: () => ({ left: 10, top: 400, width: 100, height: 20 }) };
  page.accent.set('crimson', trigger);
  assert.deepEqual(page.swapOrigin(), { x: 60, y: 410 });
});

test('a laid-out but invisible copy of the control is skipped for the visible one', () => {
  // A panel that clones its toolbar duplicates ids; an opacity-0 / off-screen copy keeps a
  // perfectly good rect, and blooming from it is exactly how the circle ends up in a corner.
  const page = wipePage({
    controls: [
      { id: 'theme-toggle', rect: { left: 0, top: 0, width: 28, height: 28 }, visible: false },
      { id: 'theme-toggle', rect: { left: -400, top: 10, width: 28, height: 28 } },   // off-screen
      TOGGLE,
    ],
  });
  page.theme.set('dark');
  assert.deepEqual(page.swapOrigin(), { x: 290, y: 40 });
});

// Dust in the wipe's wake (browser motion.js swapDustSpecs): seeded only once the transition is
// `ready`, and painted in the palette read BEFORE the swap.
test('the wipe seeds a dust layer on <body> once ready, in the OLD palette', async () => {
  const page = wipePage();
  page.theme.set('dark');
  await Promise.resolve();   // let `ready` deliver
  await Promise.resolve();
  assert.equal(page.bodyChildren.length, 1, 'one dust layer');
  const stage = page.bodyChildren[0];
  assert.equal(stage.className, 'swap-dust');
  assert.equal(stage.tagName, 'CANVAS', 'one stage, not a div per grain');
  // Sized in device pixels, laid out in CSS ones — a hi-dpi wake is not a blurry one.
  assert.deepEqual([stage.style.width, stage.style.height], ['480px', '700px']);

  // Mid-wake: grains of the accent AND its shade are in the air.
  assert.ok(page.frame(140), 'the wake is running');
  const lit = stage.fills.filter((f) => f.arcs > 0);
  const grains = lit.reduce((n, f) => n + f.arcs, 0);
  assert.ok(grains > 10, `a real field of grains (got ${grains})`);
  // Painted from the accent palette, resolved BEFORE the swap (the sandbox cannot compute
  // a colour, so color-mix() strings come through): every fourth grain wears the shade.
  assert.ok(lit.some((f) => f.colour === 'color-mix(in srgb, var(--accent) 100%, var(--accent-2))'), 'accent grains');
  assert.ok(lit.some((f) => f.colour === 'color-mix(in srgb, var(--accent) 0%, var(--accent-2))'), 'shade grains');
  // The whole point of the stage: batched fills — one per (stop, alpha step), each in
  // chunks of 32 grains (lib/cloud.js FILL_CHUNK) — not one per grain.
  assert.ok(stage.fills.length <= 6 * 8 + Math.ceil(grains / 32), `batched into ${stage.fills.length} fills, not ${grains}`);
  assert.ok(lit.every((f) => f.arcs <= 32), 'no path longer than a chunk');
  assert.ok(lit.every((f) => f.alpha > 0 && f.alpha <= 1), 'every batch carries its own alpha');

  // Nothing is alight before the ring starts moving, or after the last grain burns out.
  stage.fills.length = 0;
  assert.ok(page.frame(0));
  assert.equal(stage.fills.length, 0, 'no grain ignites at t=0 — the ring is still a point');
  assert.ok(page.frame(280 + 340 + 1) === true);
  assert.ok(stage.removed, 'the stage clears itself off the page');
});

test('the fallback path (no View Transitions) spawns no dust — there is no ring to ride', async () => {
  const page = loadAccent({ controls: [TOGGLE] });
  page.theme.set('dark');
  await Promise.resolve();
  await Promise.resolve();
  assert.equal(page.bodyChildren.length, 0);
});

test('the accent swap anchors to the toggle too, and a cross-page change animates', () => {
  const page = wipePage();
  page.accent.set('sky');
  assert.deepEqual(page.swapOrigin(), { x: 290, y: 40 });
  // A change made in ANOTHER extension page repaints here with no press at all.
  page.store.set('stencil_accent', 'brown');
  page.fireStorage('stencil_accent');
  assert.equal(page.dataAccent(), 'brown');
  assert.deepEqual(page.swapOrigin(), { x: 290, y: 40 });
});
