// js/ui/dustCloud.js — the one-canvas cloud every element-sized flight rides. The
// flights are the old tile keyframes as numbers; these pin the contract the desktop
// overlay (disintegrateOverlay.hpp legAt) and the extension's copy share.
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

// ── Particle styles: dust, water, fire ───────────────────────────────────────
test('dust is the identity style; water and fire touch a grain only mid-flight', () => {
  assert.deepEqual(PARTICLE_STYLES, { dust: 0, water: 1, fire: 2 });
  assert.deepEqual(styleFrame(STYLE_DUST, 0.5, 0.5, 0.3, 100, 250), { sx: 0, sy: 0, scale: 1, glow: 1, mix: 0 });
  for (const style of [STYLE_WATER, STYLE_FIRE]) {
    for (const p of [0, 1]) {
      const f = styleFrame(style, p, p, 0.3, 100, 250);
      assert.ok(Math.abs(f.sx) < 1e-9 && Math.abs(f.sy) < 1e-9, `style ${style} is gone at p=${p}`);
      assert.ok(Math.abs(f.scale - 1) < 1e-9 || style === STYLE_FIRE, `style ${style} is full size at p=${p}`);
    }
    const mid = styleFrame(style, 0.5, 0.5, 0.3, 100, 250);
    assert.ok(Math.abs(mid.sy) > 5, `style ${style} moves a grain off its line mid-flight`);
    assert.ok(mid.glow > 0 && mid.glow <= 1, 'brightness is a share');
    assert.ok(mid.mix >= 0 && mid.mix <= 1, 'the colour is between the accent and its shade');
  }
});

test('water sags below its line and swells; fire lifts above it and flickers', () => {
  const w = styleFrame(STYLE_WATER, 0.5, 0.5, 0.3, 100, 250);
  assert.ok(w.sy > 0 && w.sy <= WATER.sagMaxPx, `a drop sags down by up to ${WATER.sagMaxPx}px, got ${w.sy}`);
  assert.ok(Math.abs(w.sx) <= WATER.swayMaxPx);
  assert.ok(Math.abs(w.scale - (1 + WATER.swell)) < 1e-9, 'swollen fully at the midpoint');
  assert.ok(w.glow >= 1 - WATER.shimmerDepth - 1e-9);
  const f = styleFrame(STYLE_FIRE, 0.5, 0.5, 0.3, 100, 250);
  assert.ok(f.sy < 0 && -f.sy <= FIRE.liftMaxPx, `an ember lifts by up to ${FIRE.liftMaxPx}px, got ${f.sy}`);
  assert.ok(Math.abs(f.sx) <= FIRE.waverMaxPx);
  assert.ok(f.glow >= 1 - FIRE.flickerDepth - 1e-9 && f.scale >= 1 && f.scale <= 1 + FIRE.flare + 1e-9);
  // A short throw is nudged less: the push is a share of the throw, capped.
  assert.ok(styleFrame(STYLE_WATER, 0.5, 0.5, 0.3, 10, 250).sy < w.sy);
  assert.equal(styleFrame(STYLE_FIRE, 0.5, 0.5, 0.3, 0, 250).sy, 0, 'no throw, no lift');
  // Fire cools as it leaves home: accent at home, the shade far away. Water glistens on
  // its own clock instead, sweeping the whole palette.
  assert.ok(styleFrame(STYLE_FIRE, 0.5, 0, 0, 100, 0).mix < 0.1 && styleFrame(STYLE_FIRE, 0.5, 1, 1, 100, 0).mix > 0.9);
  const mixes = new Set();
  for (let t = 0; t < 2000; t += 50) mixes.add(paletteIndex(styleFrame(STYLE_WATER, 0.5, 0.5, 0.3, 100, t).mix));
  assert.ok(mixes.size >= 4, `water drifts across the palette, saw ${mixes.size} stops`);
  // Both flicker over time, and reproducibly: same hash, same frame.
  const seen = new Set();
  for (let t = 0; t < 400; t += 10) seen.add(styleFrame(STYLE_FIRE, 0.5, 0.5, 0.3, 100, t).glow.toFixed(2));
  assert.ok(seen.size > 5, 'an ember flickers');
  assert.deepEqual(styleFrame(STYLE_FIRE, 0.4, 0.4, 0.7, 80, 123), styleFrame(STYLE_FIRE, 0.4, 0.4, 0.7, 80, 123));
  assert.notDeepEqual(styleFrame(STYLE_FIRE, 0.4, 0.4, 0.7, 80, 123), styleFrame(STYLE_FIRE, 0.4, 0.4, 0.2, 80, 123));
});

test('a styled grain still sets off from and lands exactly where its flight says', () => {
  const live = { ...grain, w: 0.37, t: 1 };
  for (const style of [STYLE_WATER, STYLE_FIRE]) {
    for (const [name, f] of Object.entries(FLIGHTS)) {
      const start = moteFrame(live, name, 0, {}, 0, style), end = moteFrame(live, name, 1, {}, 900, style);
      const [a, b] = f.from === 'far' ? [[160, 120], [100, 200]] : [[100, 200], [160, 120]];
      assert.ok(near(start.x, a[0]) && near(start.y, a[1]), `${name}/${style} sets off from home or far`);
      assert.ok(near(end.x, b[0]) && near(end.y, b[1]), `${name}/${style} lands`);
      // Off the dust rail while it still has road left, and back ON it as it arrives —
      // it used to keep tens of pixels of offset all the way into the icon. Where the gap
      // is widest depends on the flight's easing, so the whole trip is scanned for it.
      let widest = 0;
      for (let p = 0.02; p < 1; p += 0.02) {
        const s = moteFrame(live, name, p, {}, 300, style), r = moteFrame(live, name, p, {}, 300, STYLE_DUST);
        widest = Math.max(widest, Math.hypot(s.x - r.x, s.y - r.y));
      }
      assert.ok(widest > 1, `${name}/${style} is off the dust rail mid-flight`);
      const late = moteFrame(live, name, 0.97, {}, 300, style);
      const lateRail = moteFrame(live, name, 0.97, {}, 300, STYLE_DUST);
      assert.ok(Math.hypot(late.x - lateRail.x, late.y - lateRail.y) < widest * 0.25,
                `${name}/${style} is back on the rail as it arrives`);
      const mid = moteFrame(live, name, 0.5, {}, 300, style), rail = moteFrame(live, name, 0.5, {}, 300, STYLE_DUST);
      assert.ok(mid.mix >= 0 && mid.mix <= 1, 'and carries its colour mix');
      assert.equal(rail.mix, dustMix(live.w, live.g), 'dust keeps its hashed stop');
    }
  }
  // Water sags DOWN the screen, fire lifts UP it, whatever way the throw points.
  const up = { ...grain, dy: -80 }, down = { ...grain, dy: 80 };
  for (const g of [up, down]) {
    assert.ok(moteFrame(g, 'scatter', 0.5, {}, 300, STYLE_WATER).y > moteFrame(g, 'scatter', 0.5, {}, 300).y);
    assert.ok(moteFrame(g, 'scatter', 0.5, {}, 300, STYLE_FIRE).y < moteFrame(g, 'scatter', 0.5, {}, 300).y);
  }
});

test('the palette is six even mixes of the accent and its shade, then the five tints', () => {
  assert.equal(PALETTE_STOPS, 6);
  const css = paletteCss();
  assert.equal(css.length, PAINT_STOPS);
  assert.equal(css[0], 'color-mix(in srgb, var(--accent) 100%, var(--accent-2))');
  assert.equal(css[5], 'color-mix(in srgb, var(--accent) 0%, var(--accent-2))');
  assert.equal(css[2], 'color-mix(in srgb, var(--accent) 60%, var(--accent-2))');
  assert.deepEqual(css.slice(6), TINT_CSS, 'the tints follow the ramp, in order');
  assert.deepEqual(TINT_CSS.slice(1, 3), ['#b4b4b4', '#6e6e6e'], 'grey and a darker grey');
  assert.match(TINT_CSS[3], /var\(--accent\) 55%, #ffffff/, 'a light accent');
  // The neutral spark and the second accent tint follow the theme (css/theme.css), since
  // white cannot be seen on a pale surface nor a deep accent on a dark one.
  assert.match(TINT_CSS[0], /^var\(--dust-ink, #\w{6}\)$/, 'the spark');
  assert.match(TINT_CSS[4], /^var\(--dust-accent-alt, #\w{6}\)$/, 'the other accent');
  const theme = readFileSync(new URL('../css/theme.css', import.meta.url), 'utf8');
  assert.match(theme, /--dust-ink:\s*#1f1f1f;[\s\S]*--dust-accent-alt:\s*color-mix\(in srgb, var\(--accent\) 55%, #000000\)/,
               'light: soot, and the accent taken down to a deep one');
  assert.match(theme, /--dust-ink:\s*#ffffff;[\s\S]*--dust-accent-alt:\s*color-mix\(in srgb, var\(--accent\) 30%, #ffffff\)/,
               'dark: white, and the accent lifted to a pale one');
  assert.equal(paletteIndex(0), 0);
  assert.equal(paletteIndex(1), 5);
  assert.equal(paletteIndex(0.5), 3);
  assert.equal(paletteIndex(2), 5, 'clamped');
});

test('two grains in three ride the accent ramp; the rest share the five tints evenly', () => {
  // The share is the contract the desktop's dustKit.hpp tintOf pins itself to.
  assert.equal(TINT_SHARE, 0.34);
  assert.equal(TINT_STOPS, 5);
  const seen = new Array(TINT_STOPS + 1).fill(0);
  let n = 0;
  for (let x = 0; x < 120; x++) {
    for (let y = 0; y < 120; y++) { seen[tintOf(hashNoise(x + 13, y + 71)) + 1]++; n++; }
  }
  const share = seen.map((c) => c / n);
  assert.ok(share[0] > 0.63 && share[0] < 0.7, `two grains in three are the accent, got ${share[0]}`);
  for (let i = 1; i <= TINT_STOPS; i++) {
    assert.ok(share[i] > 0.04 && share[i] < 0.09, `tint ${i - 1} takes a fifth of the rest, got ${share[i]}`);
  }
  // A tinted grain wears its tint whatever its mix says; the rest keep their ramp stop.
  assert.equal(tintOf(0.02), -1);
  assert.equal(stopOfTint(0.5, tintOf(0.02)), paletteIndex(0.5));
  const tinted = [...Array(200).keys()].map((i) => i / 200).find((w) => tintOf(w) >= 0);
  assert.equal(stopOfTint(0, tintOf(tinted)), PALETTE_STOPS + tintOf(tinted));
  assert.equal(stopOfTint(1, tintOf(tinted)), PALETTE_STOPS + tintOf(tinted), 'the mix cannot move it');
  // …and the pick is its own slice of the hash, not the one that chose its shape.
  const shapePick = new Set(), tintPick = new Set();
  for (let w = 0; w < 1; w += 0.01) { shapePick.add(grainShape(STYLE_WATER, w)); tintPick.add(tintOf(w)); }
  assert.ok(tintPick.size === TINT_STOPS + 1 && shapePick.size === 2);
});

test('a styled cloud is drawn from the palette, most of it on the ramp', () => {
  // The grains ignore their own colour index and ride the palette by mix — fire near home
  // is accent, water sweeps the stops — bar the tinted third, which is off the ramp.
  // drawCloud caches each grain's tint, so this also covers that path.
  const fills = [];
  const ctx = {
    globalAlpha: 1, fillStyle: '', beginPath() {}, moveTo() {}, arc() {}, ellipse() {}, lineTo() {}, closePath() {},
    fill() { fills.push(this.fillStyle); },
  };
  const ramp = ['p0', 'p1', 'p2', 'p3', 'p4', 'p5'];
  const palette = [...ramp, 't0', 't1', 't2', 't3', 't4'];
  const motes = [];
  for (let i = 0; i < 30; i++) motes.push({ ...grain, x: i * 10, c: 4, w: i / 30, t: 1, delay: 0, dur: 1000 });
  const onRamp = (f) => ramp.includes(f);
  drawCloud(ctx, motes, 'scatter', 50, palette, undefined, STYLE_FIRE);   // 5% out: still hot
  assert.ok(fills.length > 0 && fills.filter(onRamp).every((f) => f === 'p0' || f === 'p1'),
            `embers near home are accent, got ${[...new Set(fills)]}`);
  assert.ok(fills.some((f) => !onRamp(f)), 'and a few of them are tinted');
  fills.length = 0;
  drawCloud(ctx, motes, 'scatter', 500, palette, undefined, STYLE_WATER);
  assert.ok(new Set(fills.filter(onRamp)).size >= 3, 'drops glisten across the palette');
  fills.length = 0;
  drawCloud(ctx, motes, 'scatter', 500, palette, undefined, STYLE_DUST);
  assert.ok(fills.filter(onRamp).every((f) => ['p0', 'p1', 'p2', 'p3'].includes(f)),
            'dust spreads plain grains over the accent half');
  assert.ok(new Set(fills).size >= 2);
  assert.ok(new Set(fills.filter((f) => !onRamp(f))).size >= 3, 'and several tints run through it');
  fills.length = 0;
  drawCloud(ctx, motes.map((m) => ({ ...m, g: 1 })), 'scatter', 500, palette, undefined, STYLE_DUST);
  assert.ok(fills.filter(onRamp).every((f) => f === 'p5'), 'dust glints wear the shade');
  assert.equal(dustMix(0.4, 0), 0.2);
  assert.equal(dustMix(0.4, 1), 1);
});

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
