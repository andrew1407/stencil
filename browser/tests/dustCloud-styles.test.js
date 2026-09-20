// js/ui/dustCloud.js particle styles: dust, water and fire, and the accent/tint palette every
// styled cloud is drawn from. Split from dustCloud.test.js.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import {
  FLIGHTS, moteFrame, drawCloud, STYLE_DUST, STYLE_WATER, STYLE_FIRE, PARTICLE_STYLES, PALETTE_STOPS, WATER, FIRE,
  styleFrame, paletteIndex, paletteCss, dustMix, hashNoise, TINT_SHARE, TINT_STOPS, TINT_CSS, PAINT_STOPS, tintOf,
  stopOfTint, grainShape,
} from '../js/ui/dustCloud.js';

const grain = { x: 100, y: 200, dx: 60, dy: -80, mx: 40, my: -45, r: 3, s: 0.3, a: 0.9 };
const near = (a, b, eps = 1e-6) => Math.abs(a - b) < eps;

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
      // A mote leaves the dust rail while it still has road left and is back ON it as it arrives; where the gap is
      // widest depends on the flight's easing, so the whole trip is scanned for it.
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
  // The grains ignore their own colour index and ride the palette by mix — fire near home is accent, water
  // sweeps the stops — bar the tinted third, off the ramp. drawCloud caches each grain's tint.
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
