// A chat entry arrives as dust too (src/lib/motion.js chatIn, the browser twin): the veil, the
// scroller fit, the re-anchoring of a cloud in flight, and the waypoint every flight bends through.
import test from 'node:test';
import assert from 'node:assert';
import { animationsCss, motionSrc } from './helpers/sources.js';
import {
  tileMotion, DISINTEGRATE_MS,
  chatIn, CHAT_ENTER_MS, CHAT_ENTERING_CLASS, dustFitsScroller,
} from '../src/lib/motion.js';
import { FLIGHTS, moteFrame } from '../src/lib/dust/dustCloud.js';
import { classEl as el } from './helpers/listDom.js';

const css = animationsCss();

// A chat entry ARRIVES as dust too (browser motion.js chatIn twin) — without it a removed message
// had particles and an appearing one none, and the two directions read as different surfaces.
test('chatIn: veils at once, lifts only when the motes have landed', async () => {
  // One number owns both directions, and it is a SHARE of the row's flight rather than a number of
  // its own: the motes carry no text, so a long answer is unreadable until the veil lifts.
  assert.ok(CHAT_ENTER_MS < DISINTEGRATE_MS && CHAT_ENTER_MS >= DISINTEGRATE_MS / 2,
    `chat arrival ${CHAT_ENTER_MS}ms of a ${DISINTEGRATE_MS}ms row flight`);
  const row = el();
  const p = chatIn(row);
  // SYNCHRONOUSLY veiled — before the caller returns, so no frame ever paints the entry ahead of
  // its own dust.
  assert.ok(row.has(CHAT_ENTERING_CLASS), 'veiled from the first frame');
  await p;
  // No DOM here, so no motes can fly — the veil must lift at once rather than hiding the
  // entry behind a flight that never happened.
  assert.ok(!row.has(CHAT_ENTERING_CLASS), 'never left stranded invisible');
  // Decoration only: a missing element must never make an append throw.
  await chatIn(null);

  const src = motionSrc();
  // The arriving cloud is hosted in the CALLER's `host`: in the transcript its clones would read as
  // live conversation, and on <body> the `#sec-assistant .msg` rules reach none of them.
  assert.match(src, /reintegrate\(el, \{ cols, rows, hostEl: host \|\| el\.parentElement \|\| null, ms: CHAT_ENTER_MS \}\)/);
  // Two frames before the measure: frame one is the entry's layout, frame two the scroll
  // that follows it (assistant.js scrollDown pins on a rAF of its own).
  assert.match(src, /requestAnimationFrame\(\(\) => requestAnimationFrame\(fn\)\)/);
  // …and the cloud is torn down as the veil lifts, not left to its own grace period — the
  // layer holds its FINISHED state, an exact second copy over the real entry.
  assert.match(src, /unveil\(\);\s*\n\s*cancelDust\(el\);\s*\n\s*resolve\(\);/);
});

test('dustFitsScroller: only a whole entry inside its scroller may fly', () => {
  const at = (top, bottom) => ({ getBoundingClientRect: () => ({ top, bottom, width: 200, height: bottom - top }) });
  const scroller = at(100, 400);
  assert.ok(dustFitsScroller(at(120, 200), scroller), 'wholly inside');
  // The cloud is position:fixed, so the transcript does NOT clip it — an entry still
  // below the fold would scatter its motes across the composer under it.
  assert.ok(!dustFitsScroller(at(350, 460), scroller), 'hanging past the bottom');
  assert.ok(!dustFitsScroller(at(40, 150), scroller), 'hanging past the top');
  assert.ok(!dustFitsScroller(at(0, 900), scroller), 'taller than the scroller');
  assert.ok(!dustFitsScroller(null, scroller), 'no element');
  assert.ok(!dustFitsScroller(at(120, 200), null), 'no scroller');
});

test('animations/: an arriving entry is VEILED, never faded up under its own dust', () => {
  const rule = css.slice(css.indexOf('#sec-assistant .chat-transcript > .chat-entering'));
  assert.match(rule.slice(0, 400), /opacity: 0 !important;/,
    'a veil, not a keyframed fade — the entry is not seen until the motes land');
  assert.match(rule.slice(0, 400), /animation: none !important;/,
    'the dust is a photograph of where the entry IS — nothing may move under it');
  assert.match(rule.slice(0, 400), /transition: none !important;/);
  assert.match(css, /#chat-transcript > \.chat-entering/, 'the popup transcript too');
  assert.ok(!/chatCardEnter/.test(css), 'no fade-up keyframes survive');
  // The entry keeps its HEIGHT while veiled, so the transcript grows and scrolls to it.
  assert.ok(!/\.chat-entering[\s\S]{0,200}?(display: none|height: 0)/.test(rule.slice(0, 400)));
});

test('a flying cloud is re-anchored to its entry, and dropped if the entry leaves', () => {
  // The layer is position:fixed at the box measured when it launched, but the transcript SCROLLS
  // under it — a cloud left there is drawn over whatever has since moved into those coordinates.
  const src = motionSrc();
  assert.match(src, /const trackDust = \(el, ms, onDrop = \(\) => \{\}\) => \{/);
  // …and a subject that RESIZES mid-flight cannot be re-photographed, so the stale copy is dropped
  // rather than shown at the wrong size.
  assert.match(src, /const resized = !r \|\| !shot \|\|/);
  assert.match(src, /if \(!rectInScroller\(r, s\) \|\| resized\) \{ cancelDust\(el\); live = false; onDrop\(\); return; \}/);
  // Each box is measured ONCE per frame and the host writes are batched after the reads, skipping
  // unchanged values — measuring per call forced a layout per frame per flying cloud.
  assert.match(src, /const r = el\.getBoundingClientRect\?\.\(\);\s*\n\s*const s = el\.parentElement\?\.getBoundingClientRect\?\.\(\);/);
  assert.match(src, /if \(next\.left !== last\.left\) host\.style\.left = next\.left;/);
  assert.ok(/rectInScroller\(r, s\)/.test(src),
    'and is dropped outright once the entry is no longer wholly in the scroller');
  // …armed for the flight and stopped with it, so nothing keeps ticking after the cut.
  assert.match(src, /const stop = trackDust\(el, CHAT_ENTER_MS, handOver\);/);
  assert.match(src, /stop\(\);\s*\n\s*handOver\(\);/);
  // An element torn out mid-flight (the transcript cleared, a row replaced) measures as
  // a zero box, so the same check reaps its orphaned cloud within a frame.
  assert.match(src, /export function retargetDust\(el\) \{/);
});

test('a flying cloud is clipped to its scroller, so no mote lands on the composer', () => {
  // The tiles translate freely out of an `overflow: visible` host, so the clip is against the host's
  // OWN border box: the insets are signed, and negative EXPANDS it.
  const src = motionSrc();
  assert.match(src, /const clipDustToScroller = \(el, scroller = el\?\.parentElement\) => \{/);
  assert.match(src, /host\.style\.clipPath =/);
  assert.match(src, /inset\(\$\{px\(s\.top - r\.top\)\} \$\{px\(r\.right - s\.right\)\} \$\{px\(r\.bottom - s\.bottom\)\} \$\{px\(s\.left - r\.left\)\}\)/);
  // Applied before the first painted frame, then kept in step per frame (both boxes move).
  assert.match(src, /clipDustToScroller\(el\);   \/\/ before the first frame paints, not after it/);
  assert.match(src, /if \(next\.clip !== last\.clip\) host\.style\.clipPath = next\.clip;/,
    'trackDust re-clips per frame from the rects it already read');
});

test('dropping the cloud hands the entry over in the SAME frame', () => {
  // The veil is lifted by a timer at the end of the FULL flight, so a cancel that only killed the
  // motes left the message invisible until that timer fired.
  const src = motionSrc();
  assert.match(src, /const trackDust = \(el, ms, onDrop = \(\) => \{\}\) => \{/);
  assert.match(src, /cancelDust\(el\); live = false; onDrop\(\); return;/);
  assert.match(src, /const stop = trackDust\(el, CHAT_ENTER_MS, handOver\);/);
  // …and exactly once, whichever path gets there first (a drop, or the flight ending).
  assert.match(src, /if \(handedOver\) return;/);
});

// tileWaypoint itself is pinned to the app's (portParity.test.js); these are the extension's own
// carriers of it.
test('a row’s fall carries the waypoint, and the gather shares it', () => {
  const out = tileMotion(5, 3, 22, 11);
  const back = tileMotion(5, 3, 22, 11, true);
  assert.ok(Number.isInteger(out.mx) && Number.isInteger(out.my));
  assert.deepEqual([back.mx, back.my], [out.mx, out.my], 'the same bend, flown home');
});

test('every flight bends through the waypoint on its own first leg, and the cloud is one canvas', () => {
  const grain = { x: 100, y: 200, dx: 60, dy: 80, mx: 30, my: 55, r: 4, s: 0.4, a: 1 };
  for (const name of ['scatter', 'gather', 'surfaceGather', 'surfaceScatter']) {
    const f = FLIGHTS[name];
    const bend = moteFrame(grain, name, f.split);
    assert.ok(Math.abs(bend.x - 130) < 1e-6 && Math.abs(bend.y - 255) < 1e-6, `${name} passes the waypoint`);
    assert.ok(Math.abs(bend.r - 4 * (1 - (1 - 0.4) * 0.5)) < 1e-6, `${name}: half the shrink at the bend`);
    assert.notEqual(f.leg(0.5), f.rest(0.5), `${name}: leg one eases on its own`);
  }
  // No node per grain any more: the layer holds ONE canvas (lib/dustCloud.js).
  assert.match(css, /\.disintegrate-host > canvas \{ position: absolute; display: block; \}/);
  assert.ok(!/disintegrate-tile/.test(css) && !/@keyframes stTile/.test(css), 'no rule left per tile');
  // The theme wipe's grains are the same round grain, but the STAGE draws them now
  // (lib/dustWake.js spawnDust): no per-grain rule, and so no layer per grain.
  assert.ok(!/\.swap-dust-mote/.test(css) && !/swapDustMote/.test(css), 'no rule left per grain');
  assert.ok(!/will-change/.test(css.match(/\.swap-dust \{([\s\S]*?)\n\}/)[1]),
    'one layer for the whole wake, not one promoted per grain');
});
