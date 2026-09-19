// js/ui/dustCloud.js — the one-canvas cloud every element-sized flight rides. The
// flights are the old tile keyframes as numbers; these pin the contract the desktop
// overlay (DisintegrateOverlay.hpp legAt) and the extension's copy share.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import {
  bezierY, easeLut, EASE_STEPS, FLIGHTS, alphaAt, moteFrame, cloudBounds, drawCloud, ALPHA_LEVELS,
  resolveColour, startCloud, turbulenceAt, twinkleAt, TURBULENCE_MAX_PX, TWINKLE_DEPTH,
  STYLE_DUST, STYLE_WATER, STYLE_FIRE, PARTICLE_STYLES, PALETTE_STOPS, WATER, FIRE,
  styleFrame, paletteIndex, paletteCss, dustMix, hashNoise,
  TINT_SHARE, TINT_STOPS, TINT_CSS, PAINT_STOPS, tintOf, stopOfTint,
  SHAPE_DISC, SHAPE_OVAL, SHAPE_WAVE, SHAPE_TRIANGLE, SHAPE_STREAK, grainShape, headingOf, shapePolygon, addGrainPath,
  EDGE_POINTS, edgeJitter, edgeDipOf, edgeReachOf, edgeBaseOf, FILL_CHUNK, fillGrains,
} from '../js/ui/dustCloud.js';

const grain = { x: 100, y: 200, dx: 60, dy: -80, mx: 40, my: -45, r: 3, s: 0.3, a: 0.9 };
const near = (a, b, eps = 1e-6) => Math.abs(a - b) < eps;

test('a tabulated curve is pinned exactly at both ends and monotonic between', () => {
  const ease = easeLut(0.16, 1, 0.3, 1);
  assert.equal(ease(0), 0);
  assert.equal(ease(1), 1);
  assert.equal(ease(-1), 0, 'clamped below');
  assert.equal(ease(2), 1, 'clamped above');
  let prev = 0;
  for (let i = 1; i <= EASE_STEPS; i++) {
    const v = ease(i / EASE_STEPS);
    assert.ok(v >= prev - 1e-7, `monotonic at step ${i}`);
    prev = v;
  }
  // …and it is the bezier it claims to be: an ease-out is past halfway early.
  assert.ok(near(ease(0.25), bezierY(0.25, 0.16, 1, 0.3, 1), 0.01));
  assert.ok(ease(0.25) > 0.7);
});

test('every flight starts at one end of the throw, bends at split, and lands at the other', () => {
  for (const [name, f] of Object.entries(FLIGHTS)) {
    const start = moteFrame(grain, name, 0);
    const bend = moteFrame(grain, name, f.split);
    const end = moteFrame(grain, name, 1);
    const home = [100, 200], far = [160, 120], way = [140, 155];
    const [a, b] = f.from === 'far' ? [far, home] : [home, far];
    assert.deepEqual([start.x, start.y], a, `${name} sets off from its ${f.from} end`);
    assert.ok(near(bend.x, way[0]) && near(bend.y, way[1]), `${name} passes the waypoint at ${f.split}`);
    assert.deepEqual([end.x, end.y], b, `${name} lands at the other end`);
    // Size: full at home, `s` of itself at the far end, halfway between at the bend.
    const homeR = 3, farR = 3 * 0.3, midR = 3 * (1 - (1 - 0.3) * 0.5);
    assert.ok(near(start.r, f.from === 'far' ? farR : homeR), `${name} starts at the right size`);
    assert.ok(near(bend.r, midR), `${name} is half shrunk at the bend`);
    assert.ok(near(end.r, f.from === 'far' ? homeR : farR), `${name} ends at the right size`);
    // Opacity rides the stops, times the grain's own.
    assert.ok(near(start.alpha, f.alpha[0][1] * 0.9), `${name} opacity at the start`);
    assert.ok(near(end.alpha, f.alpha[f.alpha.length - 1][1] * 0.9), `${name} opacity at the end`);
  }
});

test('the flights are the keyframes they replace', () => {
  // A row's scatter: visible until it is well away, gone at the end (tileScatter).
  assert.deepEqual(FLIGHTS.scatter.alpha, [[0, 1], [0.38, 0.85], [1, 0]]);
  assert.equal(FLIGHTS.scatter.split, 0.38);
  // The gather: from nothing, fading up as it comes home (tileGather).
  assert.deepEqual(FLIGHTS.gather.alpha, [[0, 0], [0.22, 0.75], [0.58, 0.9], [1, 1]]);
  // A surface's motes ARE the window: visible from the first frame, bend early
  // (tileGatherSurface / tileScatterSurface).
  assert.equal(alphaAt(FLIGHTS.surfaceGather.alpha, 0), 0.55);
  assert.equal(FLIGHTS.surfaceGather.split, 0.16);
  assert.equal(FLIGHTS.surfaceScatter.split, 0.18);
  // A mark's fall: near-still for its first third, then dropping away (tileFall).
  assert.equal(FLIGHTS.fall.split, 0.6);
  assert.ok(Math.abs(moteFrame(grain, 'fall', 0.2).y - 200) < 0.25 * 45, 'barely moved by a fifth');
});

test('opacity stops interpolate linearly and hold at the ends', () => {
  const stops = [[0, 1], [0.5, 0.5], [1, 0]];
  assert.equal(alphaAt(stops, -1), 1);
  assert.equal(alphaAt(stops, 0.25), 0.75);
  assert.equal(alphaAt(stops, 0.5), 0.5);
  assert.equal(alphaAt(stops, 0.75), 0.25);
  assert.equal(alphaAt(stops, 2), 0);
});

test('the cloud’s bounds cover every grain’s home, bend and far end, padded by its size', () => {
  const b = cloudBounds([grain], 4);
  assert.deepEqual(b, { left: 93, top: 113, right: 167, bottom: 207 });
  assert.equal(cloudBounds([]), null);
});

test('drawing batches grains into one fill per colour and opacity step', () => {
  const calls = [];
  const ctx = {
    globalAlpha: 1, fillStyle: '',
    beginPath: () => calls.push('begin'), moveTo: () => {}, arc: () => calls.push('arc'),
    ellipse: () => calls.push('arc'), lineTo: () => {}, closePath: () => calls.push('arc'),
    fill() { calls.push(`fill ${this.fillStyle} @${this.globalAlpha.toFixed(2)}`); },
  };
  // Two palette stops: plain grains (w 0) wear the first, glints the last.
  const motes = [];
  for (let i = 0; i < 40; i++) motes.push({ ...grain, x: i * 10, w: 0, g: i % 2, delay: 0, dur: 1000 });
  drawCloud(ctx, motes, 'scatter', 100, ['red', 'blue']);
  const fills = calls.filter((c) => c.startsWith('fill'));
  assert.equal(calls.filter((c) => c === 'arc').length, 40, 'every grain is an arc');
  assert.ok(fills.length <= 2 * ALPHA_LEVELS, `${fills.length} fills for 40 grains`);
  assert.ok(fills.some((c) => c.includes('red')) && fills.some((c) => c.includes('blue')));
  assert.equal(ctx.globalAlpha, 1, 'the painter is left as it was found');
  // A grain that has faded to nothing is not drawn at all.
  calls.length = 0;
  drawCloud(ctx, [{ ...grain, c: 0, delay: 0, dur: 100 }], 'scatter', 500, ['red']);
  assert.equal(calls.filter((c) => c === 'arc').length, 0);
});

test('a plain colour passes through; a var() or color-mix() needs a probe', () => {
  assert.equal(resolveColour(null, 'rgb(1, 2, 3)'), 'rgb(1, 2, 3)');
  assert.equal(resolveColour(null, '#abc'), '#abc');
  assert.equal(resolveColour(null, 'var(--x)'), 'var(--x)', 'no document: unchanged');
  assert.equal(resolveColour(null, ''), '#888', 'nothing becomes a visible grey');
});

test('startCloud without a 2D canvas is a no-op stop, never a throw', () => {
  const host = { appendChild() {} };
  const doc = { createElement: () => ({ getContext: () => null, style: {} }) };
  const stop = startCloud(host, [grain], { span: 100, colours: ['red'], origin: { x: 0, y: 0 }, doc, raf: () => 0 });
  assert.equal(typeof stop, 'function');
  assert.doesNotThrow(stop);
  assert.equal(host.__stop, stop);
  assert.equal(typeof startCloud(host, [], { span: 1, colours: [], origin: { x: 0, y: 0 }, doc }), 'function');
});

test('startCloud paints on the canvas it appends and stops when told', () => {
  let frames = 0;
  const ctx = {
    setTransform() {}, clearRect() { frames++; }, beginPath() {}, moveTo() {}, arc() {}, fill() {},
    ellipse() {}, lineTo() {}, closePath() {},
    globalAlpha: 1, fillStyle: '',
  };
  const canvas = { getContext: () => ctx, style: {}, width: 0, height: 0 };
  const appended = [];
  const host = { appendChild: (c) => appended.push(c) };
  const doc = { createElement: () => canvas };
  const queued = [];
  let t = 0;
  const stop = startCloud(host, [{ ...grain, c: 0, delay: 0, dur: 100 }], {
    span: 100, colours: ['red'], origin: { x: 90, y: 100 }, doc, raf: (fn) => { queued.push(fn); return queued.length; },
    now: () => t, viewport: { width: 800, height: 600 },
  });
  assert.equal(appended[0], canvas, 'the canvas lives in the layer');
  assert.equal(frames, 1, 'the first frame paints synchronously');
  assert.ok(canvas.width > 0 && canvas.height > 0);
  assert.match(canvas.style.cssText, /left:3px;top:13px;/, 'seated at the bounds, relative to the layer');
  t = 50; queued.shift()();
  assert.equal(frames, 2);
  stop();
  t = 60; while (queued.length) queued.shift()();
  assert.equal(frames, 2, 'nothing paints after stop');
});

test('turbulence pushes a grain off its line mid-flight and never at either end', () => {
  const live = { ...grain, w: 0.37, t: 1 };
  assert.equal(turbulenceAt(grain, 0.5), 0, 'a grain with no turbulence share rides its rail');
  assert.equal(turbulenceAt(live, 0), 0);
  assert.ok(Math.abs(turbulenceAt(live, 1)) < 1e-9, 'lands exactly where it would have');
  let peak = 0;
  for (let p = 0.05; p < 1; p += 0.05) peak = Math.max(peak, Math.abs(turbulenceAt(live, p)));
  assert.ok(peak > 0.5 && peak <= TURBULENCE_MAX_PX, `wobbles by up to ${TURBULENCE_MAX_PX}px, got ${peak}`);
  // Reproducible: the same hash gives the same wobble, a different one a different wobble.
  assert.equal(turbulenceAt(live, 0.3), turbulenceAt({ ...live }, 0.3));
  assert.notEqual(turbulenceAt(live, 0.3), turbulenceAt({ ...live, w: 0.81 }, 0.3));
  // …and it is SIDEWAYS: the push is perpendicular to the throw, so a grain still
  // reaches its far end.
  const end = moteFrame(live, 'scatter', 1);
  assert.deepEqual([end.x, end.y], [160, 120]);
  const mid = moteFrame(live, 'scatter', 0.5), rail = moteFrame({ ...live, t: 0 }, 'scatter', 0.5);
  const off = Math.hypot(mid.x - rail.x, mid.y - rail.y);
  assert.ok(off > 0.3, 'off the rail mid-flight');
  const along = ((mid.x - rail.x) * 60 + (mid.y - rail.y) * -80) / 100;
  assert.ok(Math.abs(along) < 1e-6, 'and only sideways');
});

test('a glint twinkles on its own clock; a plain grain does not', () => {
  const glint = { ...grain, w: 0.2, g: 1 };
  assert.equal(twinkleAt(grain, 123), 1);
  const seen = new Set();
  for (let t = 0; t < 400; t += 10) {
    const k = twinkleAt(glint, t);
    assert.ok(k <= 1 + 1e-9 && k >= 1 - TWINKLE_DEPTH - 1e-9, `within the swing at ${t}ms`);
    seen.add(k.toFixed(2));
  }
  assert.ok(seen.size > 5, 'breathes over time');
  assert.ok(moteFrame(glint, 'surfaceGather', 1, {}, 0).alpha <= 0.9 + 1e-9, 'the swing rides the opacity');
});
