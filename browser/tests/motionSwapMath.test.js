// The theme-swap geometry (js/ui/motion.js): the reveal circle, its percentages, the wipe's
// own curve, the ragged edge polygon and one grain's flare/throw/shrink.
import test from 'node:test';
import assert from 'node:assert';
import {
  swapRadius, swapPercent, THEME_SWAP_MS, swapEase, swapDustSpecs, swapDustFrame, SWAP_DUST_FLARE,
  SWAP_DUST_MOTES, SWAP_DUST_MIN_T, SWAP_DUST_MAX_T, swapEdgePolygon, SWAP_EDGE_POINTS,
} from '../js/ui/motion.js';
import { STYLE_DUST, STYLE_WATER, STYLE_FIRE, edgeBaseOf } from '../js/ui/dustCloud.js';
import { ANIMATIONS_CSS } from './helpers/css.js';
import { box } from './helpers/motionRig.js';

test('swapRadius reaches the furthest viewport corner', () => {
  // From a corner the whole diagonal is needed; from the centre, half of it.
  assert.equal(swapRadius(0, 0, 300, 400), 500);
  assert.equal(swapRadius(300, 400, 300, 400), 500);
  assert.equal(swapRadius(150, 200, 300, 400), 250);
  // Off-centre: the far side wins on each axis independently.
  assert.equal(swapRadius(60, 400, 300, 400), Math.hypot(240, 400));
});

// The circle is passed in viewport PERCENTAGES: clip-path resolves them against the
// pseudo-element's own box, and a px origin would land at half its offset.
test('swapPercent expresses the circle as percentages of the viewport', () => {
  assert.deepEqual(swapPercent(0, 0, 300, 400), { x: 0, y: 0, r: 141.421 });
  assert.deepEqual(swapPercent(150, 200, 300, 400), { x: 50, y: 50, r: 70.711 });
  // A percentage radius resolves against sqrt(w² + h²) / √2 — 500px of a 300x400 box is
  // 141.421% of it, and covers the far corner exactly as the pixel radius did.
  const { r } = swapPercent(60, 400, 300, 400);
  assert.equal(Math.round((r / 100) * (Math.hypot(300, 400) / Math.SQRT2)),
    Math.round(swapRadius(60, 400, 300, 400)));
  // A viewport that cannot be measured (a stub) falls back to a centred, page-covering wipe.
  assert.deepEqual(swapPercent(10, 10, 0, 0), { x: 50, y: 50, r: 150 });
});

// swapEase evaluates the wipe's own control points — twin of the desktop's
// ThemeSwapOverlay.hpp swapEase, pinned by themeSwapEase.headless.cpp.
test('swapEase walks the wipe’s own curve, easing in slightly and never backwards', () => {
  assert.ok(Math.abs(swapEase(0)) < 1e-6, 'starts at the origin');
  assert.ok(Math.abs(swapEase(1) - 1) < 1e-6, 'ends at the full radius');
  let prev = -1;
  for (let i = 0; i <= 100; i++) {
    const v = swapEase(i / 100);
    assert.ok(v >= prev - 1e-9, `never runs backwards (t=${i / 100})`);
    prev = v;
  }
  // The area-sweep shape: the radius eases IN slightly (see the note over
  // ::view-transition-new(root) in animations.css) — behind the diagonal early on…
  assert.ok(swapEase(0.2) > 0.1 && swapEase(0.2) < 0.2, `eases in slightly (got ${swapEase(0.2)})`);
  assert.ok(swapEase(0.5) < 0.5, 'still behind the diagonal at half time');
  // …and the control points are the CSS's, verbatim.
  const css = ANIMATIONS_CSS;
  assert.ok(/animation: themeSwapReveal var\(--swap-ms, 280ms\) cubic-bezier\(0\.4, 0\.25, 0\.95, 1\)/.test(css),
    'the JS curve and the declared reveal share one set of control points');
});

// The clip is a polygon ring wearing the particle style (dustCloud.js edgeJitter); even
// its deepest dip must clear the furthest corner by the end.
test('swapEdgePolygon: a ring in the particle style that still covers the whole viewport', () => {
  const [x, y, w, h] = [90, 60, 1440, 900];
  const R = swapRadius(x, y, w, h);
  const parse = (poly) => [...poly.matchAll(/([\d.-]+)% ([\d.-]+)%/g)]
    .map((mch) => ({ x: (parseFloat(mch[1]) / 100) * w, y: (parseFloat(mch[2]) / 100) * h }));
  for (const style of [STYLE_DUST, STYLE_WATER, STYLE_FIRE]) {
    const to = parse(swapEdgePolygon(x, y, w, h, 1, style));
    assert.equal(to.length, SWAP_EDGE_POINTS);
    const radii = to.map((p) => Math.hypot(p.x - x, p.y - y));
    for (const r of radii) {
      assert.ok(r >= R - 0.05, `style ${style}: a vertex dips inside the corner reach (${r} < ${R})`);
      assert.ok(r <= R * 1.15, `style ${style}: a tongue overshoots wildly (${r})`);
    }
    const spread = Math.max(...radii) - Math.min(...radii);
    if (style === STYLE_DUST) assert.ok(spread < 0.1, 'dust: a perfect circle');
    else assert.ok(spread > R * 0.03, `style ${style}: visibly not a circle`);
    // The collapsed start: every vertex AT the origin, same count, so CSS interpolates.
    const from = parse(swapEdgePolygon(x, y, w, h, 0, style));
    assert.equal(from.length, SWAP_EDGE_POINTS);
    assert.ok(from.every((p) => Math.hypot(p.x - x, p.y - y) < 0.5));
    // Deterministic, and a stub viewport declines rather than throwing.
    assert.equal(swapEdgePolygon(x, y, w, h, 1, style), swapEdgePolygon(x, y, w, h, 1, style));
  }
  // Fire's tongues are sharp peaks with dips between; water's swells are smooth.
  const fire = parse(swapEdgePolygon(x, y, w, h, 1, STYLE_FIRE)).map((p) => Math.hypot(p.x - x, p.y - y));
  const above = fire.filter((r) => r > R * edgeBaseOf(STYLE_FIRE) * 1.04).length;
  assert.ok(above > 10 && above < fire.length * 0.6, `tongues reach well out on a minority of vertices (${above})`);
  assert.equal(swapEdgePolygon(0, 0, 0, 0, 1), '');
  // The default style is the live preference (particles → dust).
  assert.equal(swapEdgePolygon(x, y, w, h, 1), swapEdgePolygon(x, y, w, h, 1, STYLE_DUST));
});

test('swapDustSpecs seeds every mote just inside the ring, on screen, deterministically', () => {
  const [x, y, w, h] = [90, 60, 1440, 900];
  const specs = swapDustSpecs(x, y, w, h);
  assert.ok(specs.length > 30, `a real field of motes, not a sprinkle (got ${specs.length})`);
  assert.ok(specs.length <= SWAP_DUST_MOTES);
  assert.deepEqual(specs, swapDustSpecs(x, y, w, h), 'a hash, not Math.random');
  const R = swapRadius(x, y, w, h);
  for (const s of specs) {
    // Ignition rides the wipe's clock, clear of both ends.
    assert.ok(s.delay >= Math.floor(SWAP_DUST_MIN_T * THEME_SWAP_MS), `delay ${s.delay} too early`);
    assert.ok(s.delay <= Math.ceil(SWAP_DUST_MAX_T * THEME_SWAP_MS), `delay ${s.delay} too late`);
    // A mote lights only in the front's WAKE: during a view transition anything outside the
    // clip is not rendered.
    const dist = Math.hypot(s.cx - x, s.cy - y);
    const wakeAtIgnite = swapEase((s.delay + 1) / THEME_SWAP_MS) * R;   // dust: the front is the circle itself
    assert.ok(dist <= wakeAtIgnite + 1, `mote at ${dist}px ahead of the wake band at ${wakeAtIgnite}px`);
    // On screen (a hair of margin), so no spec is spent where nobody can see it.
    assert.ok(s.cx >= -16 && s.cx <= w + 16, `off screen x ${s.cx}`);
    assert.ok(s.cy >= -16 && s.cy <= h + 16, `off screen y ${s.cy}`);
    assert.ok(s.size >= 2.5 && s.size <= 6, `grain out of range ${s.size}`);
    assert.ok(s.alpha >= 0.75 && s.alpha <= 1, `never faint ${s.alpha}`);
  }
  // Both grains are present: the old surface's own colour, and the departing accent.
  assert.ok(specs.some((s) => s.accent) && specs.some((s) => !s.accent));
  // A degenerate viewport seeds nothing rather than throwing (a stub environment).
  assert.deepEqual(swapDustSpecs(0, 0, 0, 0), []);
});

test('swapDustFrame flies a grain the way its keyframes did: flare, throw, shrink', () => {
  const s = { cx: 100, cy: 200, size: 4, dx: 12, dy: -8, alpha: 0.9, delay: 40, accent: false };
  const at = (p) => swapDustFrame(s, p);
  // Ignition and burn-out are both invisible; the flare peaks where the 18% stop was.
  assert.ok(at(0).alpha < 0.01 && at(1).alpha < 0.01, 'lights up out of nothing, leaves nothing');
  assert.ok(Math.abs(at(SWAP_DUST_FLARE).alpha - s.alpha) < 1e-6, 'full brightness at the flare stop');
  // Brightness climbs to the stop and falls away after it, monotonically either side.
  for (let p = 0.02; p < SWAP_DUST_FLARE; p += 0.02) assert.ok(at(p).alpha < at(p + 0.02).alpha);
  for (let p = SWAP_DUST_FLARE; p < 0.96; p += 0.02) assert.ok(at(p).alpha > at(p + 0.02).alpha);
  // The throw is the whole of --dx/--dy, and the grain shrinks to 0.3 of itself.
  assert.deepEqual([at(0).x, at(0).y], [s.cx, s.cy], 'starts home, untransformed');
  assert.ok(Math.abs(at(1).x - (s.cx + s.dx)) < 0.05 && Math.abs(at(1).y - (s.cy + s.dy)) < 0.05);
  assert.ok(Math.abs(at(0).r - s.size / 2) < 1e-6);
  assert.ok(Math.abs(at(1).r - (s.size / 2) * 0.3) < 0.02, 'scale(0.3) at the end');
  // The curve is ease-out: more than half the throw is spent in the first half.
  assert.ok(at(0.5).x - s.cx > s.dx * 0.5);
  // It writes into a caller's object — this runs once per grain per frame.
  const out = {};
  assert.strictEqual(swapDustFrame(s, 0.5, out), out);
});
