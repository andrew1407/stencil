// The tile flight maths (js/ui/motion.js + js/ui/cloud.js): the waypoint, the span a
// late mote has left, and the batched fills one canvas cloud paints with.
import test from 'node:test';
import assert from 'node:assert';
import {
  tileWaypoint, tileMotion, surfaceMotion, WAYPOINT_ALONG, SWIRL_SHARE, SWIRL_MAX_PX,
  DUST_ALPHA_LEVELS, DISINTEGRATE_MS, MIN_TILE_MS,
} from '../../../js/ui/motion.js';
import { FLIGHTS, moteFrame } from '../../../js/ui/dust/cloud.js';
import { motionSource } from '../../helpers/motionSource.js';
import { ANIMATIONS_CSS } from '../../helpers/css.js';
import { box } from '../../helpers/motionRig.js';

test('tileWaypoint sits part-way along the throw, pushed sideways by its own noise', () => {
  const { mx, my } = tileWaypoint(100, 0, 0.9);
  assert.equal(mx, Math.round(100 * WAYPOINT_ALONG), 'along the throw');
  assert.ok(my > 0 && my <= SWIRL_MAX_PX, 'off the line, on the noise’s side');
  const other = tileWaypoint(100, 0, 0.1);
  assert.ok(other.my < 0, 'the other half of the noise bends the other way');
  assert.equal(other.mx, mx, 'the push is perpendicular — it never changes the reach');
  // A short throw bends by a SHARE of itself; a long one is capped, so a window's 400px
  // trip cannot swing its motes across half the page.
  const short = tileWaypoint(0, 40, 1);
  assert.equal(Math.abs(short.mx), Math.round(40 * SWIRL_SHARE));
  const long = tileWaypoint(0, 400, 1);
  assert.equal(Math.abs(long.mx), SWIRL_MAX_PX);
  // Dead centre of the noise is a straight line; a zero throw has nowhere to bend.
  assert.deepEqual(tileWaypoint(60, 30, 0.5), { mx: Math.round(60 * WAYPOINT_ALONG), my: Math.round(30 * WAYPOINT_ALONG) });
  assert.deepEqual(tileWaypoint(0, 0, 0.9), { mx: 0, my: 0 });
});

// The sweep's per-mote delay is folded into each flight's window
// (`t = (t - delay) / (1 - delay)`), as in the desktop overlay: the cloud is done AT the span.
test('the scatter fits inside its span: a late mote flies what is left of it, not more', () => {
  const src = motionSource();
  assert.match(src, /dur: gather \? gatherMs : Math\.max\(MIN_TILE_MS, span - m\.delay\)/,
    'the grain is given the remainder of the span, not the whole of it');
  // Every cell of a row scatter lands within DISINTEGRATE_MS (the floor is the only
  // exception, and it only ever applies to a flight far shorter than a row's).
  for (let cy = 0; cy < 16; cy++) {
    for (let cx = 0; cx < 34; cx += 7) {
      const { delay } = tileMotion(cx, cy, 34, 16);
      assert.ok(delay >= 0 && delay + Math.max(MIN_TILE_MS, DISINTEGRATE_MS - delay) <= DISINTEGRATE_MS,
        `cell ${cx},${cy} overruns the span`);
    }
  }
  // A gather is untouched: its flight is the short --gather-ms and the reversed sweep is
  // what fills the rest of the span, so it already landed on time.
  assert.ok(tileMotion(0, 0, 34, 16, true).delay >= 0);
  // …and the layer is torn down a beat after the last mote, not most of a second later.
  assert.match(src, /\}, span \+ 150\);/);
});

test('a row’s fall and a surface’s flight both carry the waypoint, deterministically', () => {
  const row = tileMotion(5, 3, 34, 16);
  assert.ok(Number.isInteger(row.mx) && Number.isInteger(row.my), 'pixel-rounded, like dx/dy');
  assert.deepEqual([row.mx, row.my], [tileMotion(5, 3, 34, 16).mx, tileMotion(5, 3, 34, 16).my], 'a hash, not Math.random');
  // The gather shares the waypoint with the scatter — the same bend, flown home.
  const back = tileMotion(5, 3, 34, 16, true);
  assert.deepEqual([back.mx, back.my], [row.mx, row.my]);
  // …and the bend scales with the throw, so a 15px tick bends as little as it flies.
  const mark = tileMotion(5, 3, 34, 16, false, 0.3);
  assert.ok(Math.hypot(mark.mx, mark.my) < Math.hypot(row.mx, row.my));
  const box = { left: 100, top: 100, width: 300, height: 200 };
  const s = surfaceMotion(2, 2, 10, 8, box, { x: 40, y: 20 });
  assert.ok(Number.isInteger(s.mx) && Number.isInteger(s.my));
  // Neighbouring cells bend to different sides: a third, decorrelated noise drives it.
  const sides = new Set();
  for (let cx = 0; cx < 10; cx++) {
    const m = surfaceMotion(cx, 2, 10, 8, box, { x: 40, y: 20 });
    // Sign of the perpendicular component, relative to the throw.
    sides.add(Math.sign(m.mx * m.dy - m.my * m.dx));
  }
  assert.ok(sides.has(1) && sides.has(-1), 'both sides of the line are used');
});

test('every flight bends through the waypoint on its own first leg, and the cloud is one canvas', () => {
  const css = ANIMATIONS_CSS;
  const grain = { x: 100, y: 200, dx: 60, dy: 80, mx: 30, my: 55, r: 4, s: 0.4, a: 1 };
  for (const name of ['scatter', 'gather', 'surfaceGather', 'surfaceScatter', 'fall']) {
    const f = FLIGHTS[name];
    // The mid keyframe: at `split` every grain is exactly at its waypoint…
    const bend = moteFrame(grain, name, f.split);
    assert.ok(Math.abs(bend.x - 130) < 1e-6 && Math.abs(bend.y - 255) < 1e-6, `${name} passes the waypoint`);
    // …at half its shrink, so nothing snaps at the bend.
    assert.ok(Math.abs(bend.r - 4 * (1 - (1 - 0.4) * 0.5)) < 1e-6, `${name}: half the shrink at the bend`);
    // The first leg carries its own curve, so the bend is a bend, not a stop-and-go
    // (a mark's fall rides one curve throughout, like the desktop's Sweep::FALL).
    if (name !== 'fall') assert.notEqual(f.leg(0.5), f.rest(0.5), `${name}: leg one eases on its own`);
  }
  // No node per grain any more: the layer holds ONE canvas (js/ui/cloud.js) and the
  // flights above are its table — nothing is left in the stylesheet per tile.
  assert.match(css, /\.disintegrate-host > canvas \{ position: absolute; display: block; \}/);
  assert.ok(!/disintegrate-tile/.test(css) && !/@keyframes tile/.test(css), 'no rule left per tile');
  // The theme wipe’s grains are the same round grain, but the STAGE draws them now
  // (js/ui/motion.js spawnSwapDust): no per-grain rule, and so no layer per grain.
  assert.ok(!/\.swap-dust-mote/.test(css) && !/swapDustMote/.test(css), 'no rule left per grain');
  const wake = css.match(/\.swap-dust \{([\s\S]*?)\n\}/)[1];
  assert.ok(!/will-change/.test(wake), 'one layer for the whole wake, not one promoted per grain');
});

test('canvas dust batches its grains: a few alpha steps, one fill per colour and step', () => {
  // Eight steps on a 3px grain are below what the eye resolves; fewer would band a
  // slow fade, more would multiply the fills the batching exists to avoid.
  assert.equal(DUST_ALPHA_LEVELS, 8);
  const motion = motionSource();
  const dust = motion.slice(motion.indexOf('const drawDust ='), motion.indexOf('const runDust ='));
  assert.match(dust, /fillGrains\(ctx, lvl\[l\], n, poly\)/, 'grains in the style\'s own shape, batched and chunked');
  assert.ok(!/drawImage\(snap, p\./.test(dust), 'never a per-grain blit of the picture');
  // The picture itself stands in for every cell still at home: one blit, then only the
  // departed cells are cleared out of it — so the front grinds, it does not pop.
  assert.match(dust, /ctx\.drawImage\(snap, sox, soy, sw \* cols, sh \* rows, 0, 0, dw \* cols, dh \* rows\)/);
  assert.match(dust, /ctx\.clearRect\(p\.x0, p\.y0, p\.x1 - p\.x0, p\.y1 - p\.y0\)/);
  // The outer edge of the grid rounds UP: the picture is blitted at its fractional size,
  // and a last column rounded down left an uncleared hairline of it down the right edge.
  const parts = motion.slice(motion.indexOf('const dustParts ='), motion.indexOf('const drawDust ='));
  assert.match(parts, /x1: cx === cols - 1 \? Math\.ceil\(cols \* dw\)/);
  assert.match(parts, /y1: cy === rows - 1 \? Math\.ceil\(rows \* dh\)/);
  assert.ok(dust.indexOf('clearRect') < dust.indexOf('const flush'), 'every clear lands before any grain is drawn');
});
