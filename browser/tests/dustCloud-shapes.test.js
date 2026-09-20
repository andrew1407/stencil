// js/ui/dustCloud.js grain shapes and the wipe front: discs, ovals, waves, triangles and
// streaks, the chunked fill and the parked-grain guard. Split from dustCloud.test.js.
import test from 'node:test';
import assert from 'node:assert';
import {
  drawCloud, STYLE_DUST, STYLE_WATER, STYLE_FIRE, SHAPE_DISC, SHAPE_OVAL, SHAPE_WAVE, SHAPE_TRIANGLE, SHAPE_STREAK,
  grainShape, headingOf, shapePolygon, addGrainPath, EDGE_POINTS, edgeJitter, edgeDipOf, edgeReachOf, edgeBaseOf,
  FILL_CHUNK, fillGrains,
} from '../js/ui/dustCloud.js';

const grain = { x: 100, y: 200, dx: 60, dy: -80, mx: 40, my: -45, r: 3, s: 0.3, a: 0.9 };
const near = (a, b, eps = 1e-6) => Math.abs(a - b) < eps;

// ── Grain shapes and the wipe's front ───────────────────────────────────────
test('each style has its own grains: dust discs, water ovals and wave lines, fire triangles and sparks', () => {
  for (const w of [0, 0.1, 0.37, 0.5, 0.8, 0.95]) assert.equal(grainShape(STYLE_DUST, w), SHAPE_DISC);
  const water = new Set(), fire = new Set();
  for (let w = 0; w < 1; w += 0.01) { water.add(grainShape(STYLE_WATER, w)); fire.add(grainShape(STYLE_FIRE, w)); }
  assert.deepEqual([...water].sort(), [SHAPE_OVAL, SHAPE_WAVE]);
  assert.deepEqual([...fire].sort(), [SHAPE_TRIANGLE, SHAPE_STREAK]);
  // Pinned picks — the desktop's grainShape must agree (motionPrefs.headless.cpp).
  assert.equal(grainShape(STYLE_WATER, 0.1), SHAPE_OVAL);
  assert.equal(grainShape(STYLE_WATER, 0.8), SHAPE_WAVE);
  assert.equal(grainShape(STYLE_FIRE, 0.1), SHAPE_TRIANGLE);
  assert.equal(grainShape(STYLE_FIRE, 0.8), SHAPE_STREAK);
  // A grain lies along its heading; a gather flies its throw backwards.
  assert.ok(near(headingOf(0, 1, false), Math.PI / 2) && near(headingOf(0, 1, true), 3 * Math.PI / 2));
  // Polygons: a triangle points along the heading (its tip is ahead of its base).
  const tri = shapePolygon(SHAPE_TRIANGLE, 10, 20, 2, 0.5);
  assert.deepEqual(tri.map((v) => +v.toFixed(6)), [12.983781, 21.630047, 7.549259, 20.940142, 9.466961, 17.429811]);
  assert.equal(shapePolygon(SHAPE_WAVE, 0, 0, 2, 0).length, 18 * 2, 'a wave line is a 9-sample ribbon');
  const streak = shapePolygon(SHAPE_STREAK, 0, 0, 2, 0);
  assert.equal(streak.length, 8);
  assert.ok(Math.abs(streak[1] - streak[7]) > Math.abs(streak[3] - streak[5]), 'a spark is wide at the head, thin at the tail');
  // drawCloud works every grain's shape and heading out once per cloud, not per frame.
  const scratch = { out: {}, buckets: new Map() };
  const ctx = { globalAlpha: 1, fillStyle: '', beginPath() {}, moveTo() {}, arc() {}, ellipse() {}, lineTo() {}, closePath() {}, fill() {} };
  drawCloud(ctx, [{ ...grain, w: 0.8, delay: 0, dur: 1000 }], 'surfaceGather', 300, ['p'], scratch, STYLE_FIRE);
  assert.deepEqual([...scratch.shapes], [SHAPE_STREAK]);
  assert.ok(near(scratch.heads[0], Math.atan2(-80, 60) + Math.PI, 1e-6), 'a gather points home');
});

test('a batch is filled in short chunks — the engine charges more per grain the longer the path', () => {
  assert.equal(FILL_CHUNK, 32);
  const ops = [];
  const ctx = { beginPath: () => ops.push('b'), fill: () => ops.push('f'), moveTo() {}, arc: () => ops.push('g'), ellipse() {}, lineTo() {}, closePath() {} };
  const b = new Float32Array(70 * 5);
  fillGrains(ctx, b, 70, []);
  assert.equal(ops.filter((o) => o === 'b').length, 3, '70 grains → 32 + 32 + 6');
  assert.equal(ops.filter((o) => o === 'f').length, 3);
  assert.equal(ops.filter((o) => o === 'g').length, 70, 'every grain drawn once');
});

test('addGrainPath draws discs as arcs, ovals as ellipses and the rest as closed polygons', () => {
  const ops = [];
  const ctx = { moveTo: () => ops.push('m'), arc: () => ops.push('arc'), ellipse: () => ops.push('ellipse'),
                lineTo: () => ops.push('l'), closePath: () => ops.push('z') };
  addGrainPath(ctx, SHAPE_DISC, 0, 0, 2, 0); assert.deepEqual(ops, ['m', 'arc']); ops.length = 0;
  addGrainPath(ctx, SHAPE_OVAL, 0, 0, 2, 1); assert.deepEqual(ops, ['m', 'ellipse']); ops.length = 0;
  addGrainPath(ctx, SHAPE_TRIANGLE, 0, 0, 2, 1); assert.deepEqual(ops, ['m', 'l', 'l', 'z']); ops.length = 0;
  addGrainPath(ctx, SHAPE_STREAK, 0, 0, 2, 1); assert.deepEqual(ops, ['m', 'l', 'l', 'l', 'z']); ops.length = 0;
  addGrainPath(ctx, SHAPE_WAVE, 0, 0, 2, 1); assert.equal(ops.filter((o) => o === 'l').length, 17);
});

test('the wipe front wears the style: dust a perfect circle, water waved, fire cut into tongues', () => {
  for (let k = 0; k < EDGE_POINTS; k++) assert.equal(edgeJitter(STYLE_DUST, k), 0);
  const water = [], fire = [];
  for (let k = 0; k < EDGE_POINTS; k++) { water.push(edgeJitter(STYLE_WATER, k)); fire.push(edgeJitter(STYLE_FIRE, k)); }
  assert.ok(Math.max(...water) > 0.02 && Math.min(...water) < -0.02, 'water swells both ways');
  assert.ok(Math.max(...water) <= edgeReachOf(STYLE_WATER) && -Math.min(...water) <= edgeDipOf(STYLE_WATER));
  assert.ok(Math.max(...fire) > 0.05, 'fire reaches out in tongues');
  assert.ok(Math.max(...fire) <= edgeReachOf(STYLE_FIRE) + 1e-9 && -Math.min(...fire) <= edgeDipOf(STYLE_FIRE) + 1e-9);
  assert.ok(fire.filter((j) => j > 0.04).length < EDGE_POINTS / 2, 'tongues, not a bigger circle');
  // Pinned vertices — the desktop's edgeJitter must agree (themeSwapEase.headless.cpp).
  assert.ok(near(edgeJitter(STYLE_WATER, 17), -0.015235022, 1e-9));
  assert.ok(near(edgeJitter(STYLE_FIRE, 17), 0.043404486, 1e-9));
  assert.equal(edgeBaseOf(STYLE_DUST), 1.012);
  assert.ok(near(edgeBaseOf(STYLE_WATER), 1 + 0.036 + 0.012));
});

test('a gathering grain draws nothing until it sets off — no blob of parked grains on the icon', () => {
  const arcs = [];
  const ctx = { globalAlpha: 1, fillStyle: '', beginPath() {}, moveTo() {}, arc: (x, y) => arcs.push([x, y]), ellipse() {}, lineTo() {}, closePath() {}, fill() {} };
  const motes = [];
  for (let i = 0; i < 20; i++) motes.push({ ...grain, x: i * 10, w: 0.5, delay: 200 + i * 10, dur: 400 });
  drawCloud(ctx, motes, 'surfaceGather', 100, ['p']);
  assert.equal(arcs.length, 0, 'before any delay is up, the far end shows no grain');
  drawCloud(ctx, motes, 'surfaceGather', 250, ['p']);
  assert.ok(arcs.length > 0 && arcs.length < 20, `only the launched grains show (${arcs.length})`);
  // A scatter still holds its unlaunched grains at home (they ARE the surface there).
  arcs.length = 0;
  drawCloud(ctx, motes, 'surfaceScatter', 100, ['p']);
  assert.equal(arcs.length, 20);
});
