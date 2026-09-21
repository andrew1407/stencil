import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { contextMenuSource } from './helpers/contextMenuSource.js';
import { spawnCount, newMote, edgePoint, stepMote, moteAlpha, parseRgb, mixRgb, dustPalette, ringRadius, ringAlpha, ringAngles, layerAbove,
         DUST_SILENCE, DUST_RATE, DUST_LIFE_MS, DUST_TINTS, RING_SPOKES, RING_SPIN_MS, RING_BEAT_MS } from '../js/ui/dust/voiceDust.js';

// While a mic listens, motes leave its tile with the voice: none in silence, a trickle on
// quiet speech, a swarm on a shout — the rate follows the level squared.
test('spawnCount: silence spawns nothing, the rate grows with the level squared, dt scales it', () => {
  const never = () => 1;   // the fractional roll never adds one
  assert.equal(spawnCount(0, 16.7, never), 0);
  assert.equal(spawnCount(DUST_SILENCE, 16.7, never), 0);
  assert.equal(spawnCount(1, 16.7, never), DUST_RATE);
  assert.equal(spawnCount(0.5, 16.7, never), Math.floor(DUST_RATE / 4));
  assert.equal(spawnCount(1, 33.4, never), DUST_RATE * 2, 'a dropped frame spawns for the time it covered');
  // A low rate still yields the odd mote: the fraction is rolled.
  assert.equal(spawnCount(0.1, 16.7, () => 0), 1);
  assert.equal(spawnCount(0.1, 16.7, () => 1), 0);
});

test('newMote: born on the tile edge in EVERY direction, flying outward, faster and larger when louder', () => {
  const seq = (vals) => { let i = 0; return () => vals[i++ % vals.length]; };
  // The edge point: a ray meets the side it points at, corners included.
  assert.deepEqual(edgePoint(40, 32, 0), { x: 20, y: 0 });
  const up = edgePoint(40, 32, -Math.PI / 2);
  assert.ok(Math.abs(up.x) < 1e-9 && Math.abs(up.y + 16) < 1e-9);
  const diag = edgePoint(40, 32, Math.atan2(16, 20));   // straight at the bottom-right corner
  assert.ok(Math.abs(diag.x - 20) < 1e-9 && Math.abs(diag.y - 16) < 1e-9, 'a corner ray is born on the corner');
  // A mote aimed at the top-left quadrant is born there and flies further into it.
  const q = (Math.PI * 5) / 4;   // 225°
  const m = newMote(40, 32, 0, seq([q / (2 * Math.PI), 0.5, 0.5]));
  assert.ok(m.x < 0 && m.y < 0, 'born in the top-left');
  assert.ok(m.vx < 0 && m.vy < 0, 'and flying up-left');
  assert.ok(m.x * m.vx + m.y * m.vy > 0, 'outward: the velocity points away from the centre');
  const quiet = newMote(40, 32, 0.1, seq([0.1, 0.5, 0.5, 0.5, 0.5, 0.5]));
  const loud = newMote(40, 32, 1, seq([0.1, 0.5, 0.5, 0.5, 0.5, 0.5]));
  assert.ok(Math.hypot(loud.vx, loud.vy) > Math.hypot(quiet.vx, quiet.vy) && loud.size > quiet.size, 'a shout flings bigger motes further');
  assert.ok(quiet.life >= DUST_LIFE_MS[0] && quiet.life <= DUST_LIFE_MS[1]);
});

test('stepMote drifts with drag and dies at the end of its life; alpha rises fast then fades', () => {
  const m = { x: 20, y: 0, vx: 100, vy: 0, age: 0, life: 500, size: 2 };
  assert.ok(stepMote(m, 100));
  assert.ok(m.x > 20 && m.vx < 100, 'moved outward and slowed');
  assert.ok(moteAlpha(m) > 0.8, 'lit after the quick fade-in');
  assert.ok(stepMote(m, 300));
  assert.ok(moteAlpha(m) < 0.4 && moteAlpha(m) > 0, 'fading out');
  assert.ok(!stepMote(m, 200), 'spent');
});

// The three mic faces wear it, keyed on their own listening class; the level is the
// toolbar's live --voice-level, so no second voice subscription exists.
test('every mic face attaches the dust off its listening class; the level is --voice-level', () => {
  const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
  assert.ok(read('../js/ui/visuals/voiceToggle.js').includes("attachVoiceDust(btn, () => btn.classList.contains('active'))"));
  assert.ok(read('../js/ui/chat/chatPanel.js').includes("attachVoiceDust(sendBtn, () => sendBtn.classList.contains('chat-voice-listening'))"));
  assert.ok(contextMenuSource().includes("attachVoiceDust(sendBtn, () => sendBtn.classList.contains('chat-voice-listening'))"));
  const dust = read('../js/ui/dust/voiceDust.js');
  assert.ok(dust.includes("getPropertyValue('--voice-level')"));
  assert.ok(dust.includes('dustEnabled()'), 'no dust under reduced motion, nor in a mode without particles');
  // Behind the icon: the tile's rounded rectangle is punched out of every frame.
  assert.ok(dust.includes("ctx.globalCompositeOperation = 'destination-out'") && dust.includes('ctx.roundRect(DUST_MARGIN, DUST_MARGIN, r.width, r.height, rad)'));
  // …and ONLY the icon: the cloud rides over the neighbouring controls, never hides behind them.
  assert.ok(!dust.includes('parentElement?.children') && !dust.includes('parentElement.children'), 'no neighbour is punched out');
});

// Every mote wears its own tint from the theme's range — ink through greys and the light accent to the full
// accent, the window dust's idea — so the cloud glints instead of reading as one flat colour.
test('dustPalette runs from the theme ink to the accent; motes pick a stop each', () => {
  assert.deepEqual(parseRgb('rgb(255, 255, 255)'), [255, 255, 255]);
  assert.deepEqual(parseRgb('rgba(124, 58, 237, 0.5)'), [124, 58, 237]);
  assert.deepEqual(parseRgb('#7c3aed'), [124, 58, 237]);
  assert.equal(parseRgb('var(--accent)'), null);
  assert.deepEqual(mixRgb([0, 0, 0], [100, 200, 50], 0.5), [50, 100, 25]);
  const dark = dustPalette('rgb(255, 255, 255)', 'rgb(124, 58, 237)');
  assert.equal(dark.length, DUST_TINTS);
  assert.equal(dark[0], 'rgb(255, 255, 255)', 'starts white on a dark theme');
  assert.equal(dark.at(-1), 'rgb(124, 58, 237)', 'ends on the accent');
  assert.equal(dark[2], 'rgb(203, 176, 248)', 'a light violet in between');
  const light = dustPalette('rgb(20, 20, 20)', '#7c3aed');
  assert.equal(light[0], 'rgb(20, 20, 20)', 'starts on the dark ink on a light theme');
  // Unparseable colours never break the loop.
  assert.deepEqual(dustPalette('rgb(1, 1, 1)', 'var(--accent)'), ['var(--accent)']);
  assert.deepEqual(dustPalette('nope', '#7c3aed'), ['rgb(124, 58, 237)']);
  const seq = (vals) => { let i = 0; return () => vals[i++ % vals.length]; };
  const m = newMote(40, 32, 0.5, seq([0.3, 0.5, 0.5, 0.5, 0.5, 0.5, 0.99]));
  assert.ok(Number.isInteger(m.tint) && m.tint >= 0 && m.tint < DUST_TINTS);
  const src = readFileSync(new URL('../js/ui/dust/voiceDust.js', import.meta.url), 'utf8');
  assert.ok(src.includes('ctx.fillStyle = palette[m.tint % palette.length]'), 'each mote is painted in its own stop');
  assert.ok(src.includes("probe.style.color = 'var(--text-main)'") && src.includes("probe.style.color = 'var(--accent)'"), 'the range comes from the theme');
});

// The ray ring rides the same canvas as the dust: 24 hairlines from the tile's centre, one turn per 8s,
// breathing 0.35 ↔ 0.6 on the 1.2s beat, clipped by the tile punch-out so they start at its edge.
test('ring geometry: 24 spokes turning on the logo clocks, reach and shimmer as specified', () => {
  assert.equal(RING_SPOKES, 24);
  assert.equal(ringAngles(0).length, 24);
  assert.ok(Math.abs(ringAngles(RING_SPIN_MS / 4)[0] - Math.PI / 2) < 1e-9, 'a quarter turn per quarter revolution');
  assert.ok(Math.abs(ringAngles(0)[1] - ringAngles(0)[0] - (2 * Math.PI) / 24) < 1e-9, 'evenly spaced');
  assert.ok(Math.abs(ringAlpha(0) - 0.35) < 1e-9 && Math.abs(ringAlpha(RING_BEAT_MS / 2) - 0.6) < 1e-9);
  assert.ok(ringRadius(40, 32, 1) > ringRadius(40, 32, 0) && ringRadius(40, 32, 0) > Math.hypot(20, 16), 'past the corners, further when loud');
  const src = readFileSync(new URL('../js/ui/dust/voiceDust.js', import.meta.url), 'utf8');
  const ring = src.indexOf('for (const a of ringAngles(now))'), punch = src.indexOf("ctx.globalCompositeOperation = 'destination-out'");
  assert.ok(ring > 0 && punch > ring, 'the ring is drawn before the tile is punched out — so it sits behind the icon');
  assert.ok(src.includes('ctx.moveTo(cx, cy);'), 'spokes start at the centre; the punch-out cuts them to the edge');
});

// The canvas paints one layer above the highest z-index on its wearer's ancestor chain: the chat panel and
// the flyout stack far over the page, and a body-level canvas at a small fixed one vanished (user report).
test('layerAbove: one over the wearer\'s tallest ancestor layer, never below 5', () => {
  const node = (z, parent = null) => ({ nodeType: 1, z, parentElement: parent });
  const get = (n) => ({ zIndex: n.z });
  const page = node('auto');
  assert.equal(layerAbove(node('auto', page), get), 5, 'no stacking anywhere → the old floor');
  const panel = node('120', page);
  assert.equal(layerAbove(node('auto', node('2', panel)), get), 121, 'above the panel it lives in');
  assert.equal(layerAbove(node('3', page), get), 5, 'a small own layer still lands on the floor');
  const src = readFileSync(new URL('../js/ui/dust/voiceDust.js', import.meta.url), 'utf8');
  assert.ok(src.includes('zIndex: String(layerAbove(el))'), 'the canvas takes that layer');
});
