// Scroll reveal + drop landing (src/lib/motion.js) and the CSS that drives them —
// the extension half of the shared contract (browser/tests/motion.test.js is the twin).
// Node has no IntersectionObserver, so the observer is pinned against stub observers
// over a hand-rolled DOM; the CSS contract is read straight out of the stylesheet.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { animationsCss, popupCss, motionSrc } from './helpers/sources.js';

// The stylesheet the CSS half of this contract lives in, read once.
const css = animationsCss();

import {
  observeReveal, flashLanding,
  createListHold, emptyStateVisible, tileMotion, materialize,
  MATERIALIZE_CLASS, MATERIALIZE_VEIL_CLASS, LEAVE_MS, DISINTEGRATE_MS,
  chatIn, CHAT_ENTER_MS, CHAT_ENTERING_CLASS, dustFitsScroller,
  diffListKeys, createFilterTransition, filterLeave,
  FILTER_IN_CLASS, FILTER_OUT_CLASS, FILTER_ENTER_MS, FILTER_LEAVE_MS, TILE_JITTER_SHARE,
} from '../src/lib/motion.js';
import { FLIGHTS, moteFrame, alphaAt } from '../src/lib/dustCloud.js';
import { makeList, makeRow, renderKeys, fakeTimers } from './helpers/listDom.js';

// A minimal element stand-in for the class-toggling helpers below.
const el = (cls = '') => {
  const classes = new Set(cls ? cls.split(' ') : []);
  return {
    offsetWidth: 0,
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
      contains: (c) => classes.has(c),
      toggle: (c, on) => (on ? classes.add(c) : classes.delete(c)),
    },
    has: (c) => classes.has(c),
  };
};

// ── The reveal ramp ─────────────────────────────────────────────────────────
// revealDissolve/revealGrain are shared with the app to the letter (portParity.test.js
// pins them), so their cases are the browser suite's.

test('observeReveal is inert without requestAnimationFrame', () => {
  const prior = globalThis.requestAnimationFrame;
  delete globalThis.requestAnimationFrame;
  try {
    const stop = observeReveal({ addEventListener() {} }, '.row');
    assert.equal(typeof stop, 'function');
    stop();
  } finally { globalThis.requestAnimationFrame = prior; }
});

test('flashLanding replays on a second drop and clears itself', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const row = el();
  flashLanding(row);
  assert.ok(row.has('just-dropped'), 'defaults to the drop-landing class');
  flashLanding(row, 'just-dropped', 900);
  t.mock.timers.tick(899);
  assert.ok(row.has('just-dropped'), 'the first timer was cancelled, not left to fire early');
  t.mock.timers.tick(2);
  assert.ok(!row.has('just-dropped'));
});

test('animations/: reveal rest state, drop landing, drag cue, reduced motion', () => {
  const rest = css.slice(css.indexOf('.reveal-item.reveal-masked {'),
                         css.indexOf('\n}', css.indexOf('.reveal-item.reveal-masked {')));
  const base = css.slice(css.indexOf('.reveal-item {'), css.indexOf('\n}', css.indexOf('.reveal-item {')));
  assert.ok(!/mask-image/.test(base), 'a settled row composites no mask at all');
  // A mask dissolve, not per-particle clones: scrolling is continuous, so the reveal
  // must not churn DOM the way a one-shot removal can (browser parity).
  assert.ok(/--dissolve: 1/.test(base), 'out-of-view rows rest fully dissolved');
  assert.ok(/var\(--vis-start/.test(rest) && /var\(--vis-end/.test(rest),
    'the wipe is anchored to the still-visible span, so a readable row is untouched');
  assert.ok(/@property --dissolve \{ syntax: "<number>"/.test(css), '--dissolve is registered, so it can transition');
  assert.ok(/transition: --dissolve/.test(base) && /transform: none/.test(base));
  assert.ok(/mask-composite: add/.test(rest), 'grain UNIONed with the wipe, so a settled row is solid');
  assert.ok(/\.reveal-item\.reveal-in \{ --dissolve: 0; transform: none; \}/.test(css));
  // Rows must NOT also carry `animation: stRowIn … both`: the filled end state would
  // out-rank the reveal's transform and strand every row at its rest state.
  assert.ok(!/\.list \.row \{ animation: stRowIn/.test(css), 'list rows ride the reveal, not stRowIn');
  // A row a DROP created lands in and pulses the accent ring.
  assert.ok(/\.row\.just-dropped \{[\s\S]*?animation: stDropLand[\s\S]*?stDropRing/.test(css));
  assert.ok(/\.list\.drag-over \{ animation: stDragCue/.test(css), 'the drop cue breathes while a drag hovers');
  assert.ok(/\.reveal-item \{ --dissolve: 0 !important;[\s\S]{0,160}?mask-image: none !important; \}/
    .test(css.slice(css.indexOf('@media (prefers-reduced-motion: reduce)'))),
    'reduced motion shows every row whole, mask and all');
});

test('animations/: only the wipe drives the theme transition', () => {
  assert.match(css, /::view-transition-group\(root\) \{ animation: none; \}/,
    'the UA group default would retime the snapshots under the wipe');
});

test('popup.css keeps just-pinned distinct from the drop landing', () => {
  const css = popupCss();   // shadows the animations/ read above
  assert.ok(/\.row\.just-pinned \{ animation: stencil-pin-flash/.test(css),
    'pinning an EXISTING row keeps the plainer flash');
  // The transcript's per-entry entrance moved to the shared reveal; a leftover
  // `animation: … both` here would out-rank it.
  assert.ok(!/#chat-transcript > \* \{ animation:/.test(css), 'transcript entries ride the reveal');
});

// ── The wipe hold + refresh gate (browser connectModal.test.js twin) ────────
// The options page's server-connections list defers its storage.onChanged rebuild
// and its "No servers connected yet." empty state while a row's leave (or a new
// row's materialize) is still playing — createListHold is that gate.

const stubTimers = () => {
  const queue = [];
  return {
    queue,
    setTimer: (fn, ms) => { queue.push({ fn, ms }); return queue.length; },
    run: () => { for (const t of queue.splice(0)) t.fn(); },
  };
};

test('createListHold: holds until the wipe is over, then settles exactly once', async () => {
  const t = stubTimers();
  let settles = 0;
  const hold = createListHold({ settle: () => settles++, wait: () => 900, setTimer: t.setTimer });
  assert.equal(hold.holding, false);
  const settle = hold.begin();
  assert.equal(hold.holding, true, 'a wipe in flight gates the onChanged re-render');
  assert.equal(settles, 0, 'no settle render before the dust has landed');
  const p = settle();
  assert.equal(t.queue[0].ms, 900, 'waits the FULL wipe, not the short collapse');
  t.run();
  await p;
  assert.equal(settles, 1);
  assert.equal(hold.holding, false);
});

test('createListHold: finalizeAll settles pending holds NOW; late timers no-op', async () => {
  const t = stubTimers();
  let settles = 0;
  const hold = createListHold({ settle: () => settles++, wait: () => 900, setTimer: t.setTimer });
  const a = hold.begin();
  hold.begin();
  const pa = a();
  hold.finalizeAll();
  assert.equal(settles, 2, 'a view closing mid-animation finalizes immediately');
  assert.equal(hold.holding, false);
  t.run();
  await pa;
  assert.equal(settles, 2, 'a finalized hold’s timer settles nothing twice');
});

test('emptyStateVisible: never during a wipe, only once truly settled', () => {
  assert.equal(emptyStateVisible(0, false), true, 'empty + idle → placeholder');
  assert.equal(emptyStateVisible(0, true), false,
    'empty but mid-wipe → the placeholder waits for the settle render');
  assert.equal(emptyStateVisible(2, false), false);
  assert.equal(emptyStateVisible(2, true), false);
  assert.equal(emptyStateVisible(0), true, 'holding defaults to false');
});

// ── The gather (materialize = the removal reversed) ─────────────────────────

test('tileMotion reverse: same flight path, inverted sweep', () => {
  const cols = 22;
  const rows = 11;
  const outTop = tileMotion(3, 0, cols, rows);
  const backTop = tileMotion(3, 0, cols, rows, true);
  assert.equal(outTop.dx, backTop.dx);
  assert.equal(outTop.dy, backTop.dy);
  assert.equal(outTop.rot, backTop.rot);
  assert.equal(outTop.scale, backTop.scale);
  const outBottom = tileMotion(3, rows - 1, cols, rows);
  const backBottom = tileMotion(3, rows - 1, cols, rows, true);
  assert.ok(outTop.delay < outBottom.delay, 'scatter sweeps top→bottom');
  assert.ok(backTop.delay > backBottom.delay, 'gather sweeps bottom→top');
  // The gather's 0.4 share plus a mote's own jitter, which is a SHARE of the span too —
  // it used to be a flat 60ms, and that literal quietly became wrong the moment the span
  // changed (motion.js TILE_JITTER_SHARE).
  assert.ok(backTop.delay <= DISINTEGRATE_MS * (0.4 + TILE_JITTER_SHARE),
    'same sweep window as the scatter');
});

test('materialize: expands on the collapse’s own timer; no veil without dust', async () => {
  // No DOM here, so the gather builds no tiles (disintegrate bails) — the row must
  // then expand un-veiled on LEAVE_MS, never hide behind a veil nothing will lift.
  const row = el();
  const started = Date.now();
  const p = materialize(row);
  assert.ok(row.has(MATERIALIZE_CLASS), 'the box-expand class goes on immediately');
  assert.ok(!row.has(MATERIALIZE_VEIL_CLASS), 'no dust → no veil (nothing would lift it)');
  await p;
  assert.ok(!row.has(MATERIALIZE_CLASS), 'cleaned up once the expansion is over');
  assert.ok(Date.now() - started >= LEAVE_MS - 20, 'the expansion runs the collapse’s duration');
});

test('materialize: a missing element resolves without touching anything', async () => {
  await materialize(null);   // must not throw — the add never depends on the animation
});

test('animations/: materialize is the leave reversed, veil outranks keyframes', () => {
  assert.match(css, /\.materializing \{[^}]*animation: stRowMaterialize 0\.32s cubic-bezier\(0\.16, 1, 0\.3, 1\)/,
    'the box opens on one expo-out curve — 220ms of ease-out read as a pop');
  assert.match(css, /@keyframes stRowMaterialize \{\s*from \{ opacity: 0;[^}]*max-height: 0/,
    'the expansion starts from the collapsed end-state of stRowLeave');
  assert.match(css, /\.materialize-veil \{ opacity: 0 !important; \}/,
    'the veil must outrank stRowMaterialize’s animated opacity');
  // The gather is the scatter reversed, flown on the one canvas (lib/dustCloud.js): a
  // grain starts where the scatter would have flung it, invisible, and flies home to
  // identity at full opacity — the stTileGather keyframes, as numbers.
  assert.equal(FLIGHTS.gather.from, 'far', 'a gather grain starts where the scatter would have flung it');
  const grain = { x: 10, y: 20, dx: 30, dy: 40, mx: 18, my: 25, r: 3, s: 0.5, a: 1 };
  assert.deepEqual([moteFrame(grain, 'gather', 0).x, moteFrame(grain, 'gather', 0).y], [40, 60]);
  const home = moteFrame(grain, 'gather', 1);
  assert.deepEqual([home.x, home.y, home.r], [10, 20, 3], 'and flies home to identity');
  assert.equal(alphaAt(FLIGHTS.gather.alpha, 0), 0);
  assert.equal(alphaAt(FLIGHTS.gather.alpha, 1), 1);
});

// ── Filtering a list in and out (src/lib/motion.js createFilterTransition) ──
// Every filtered list in the extension (the popup's scanned images, the options page's
// pins and connections, editor mode's open editors + pages) is rebuilt wholesale on
// each change. The transition is what makes that read symmetrically: the rows the
// filter dropped play out, the ones it admitted play in. The list DOM and the clock are
// stubs (tests/helpers/listDom.js) so "after the animation" is a fact, not a sleep.

test('diffListKeys: only real arrivals and departures, in render order', () => {
  assert.deepEqual(diffListKeys(['a', 'b', 'c'], ['b', 'd']), { entered: ['d'], left: ['a', 'c'] });
  assert.deepEqual(diffListKeys([], ['a']), { entered: ['a'], left: [] });
  assert.deepEqual(diffListKeys(['a'], []), { entered: [], left: ['a'] });
});

test('diffListKeys: a REORDER is not a change — nothing enters, nothing leaves', () => {
  assert.deepEqual(diffListKeys(['a', 'b', 'c'], ['c', 'a', 'b']), { entered: [], left: [] });
});

test('a filter change plays BOTH ways: the dropped rows leave, the new ones enter', () => {
  const clock = fakeTimers();
  const list = makeList();
  const tr = createFilterTransition({ list, reduced: () => false, setTimer: clock.setTimer, clearTimer: clock.clearTimer });

  renderKeys(list, tr, ['a', 'b', 'c']);
  assert.deepEqual(list.keys(), ['a', 'b', 'c']);
  clock.advance(FILTER_ENTER_MS + 100);

  const diff = renderKeys(list, tr, ['b', 'd']);
  assert.deepEqual(diff, { entered: ['d'], left: ['a', 'c'] });
  // The departed rows are back on screen, where they stood, playing their exit…
  assert.deepEqual(list.keys(), ['a', 'b', 'c', 'd'], 'ghosts sit back among the rows they stood between');
  const byKey = Object.fromEntries(list.children.map((el) => [el.dataset.key, el]));
  assert.ok(byKey.a.classList.contains(FILTER_OUT_CLASS) && byKey.c.classList.contains(FILTER_OUT_CLASS));
  assert.ok(byKey.d.classList.contains(FILTER_IN_CLASS), 'the arriving row ramps in');
  assert.ok(!byKey.b.classList.contains(FILTER_IN_CLASS), 'a row that merely stayed is not re-animated');
  assert.ok(!byKey.b.classList.contains(FILTER_OUT_CLASS));
  // …and once it is over the list holds EXACTLY the filtered set.
  clock.advance(FILTER_LEAVE_MS + FILTER_ENTER_MS + 100);
  assert.deepEqual(list.keys(), ['b', 'd']);
  assert.equal(tr.ghostCount, 0);
  assert.ok(!byKey.d.classList.contains(FILTER_IN_CLASS), 'the enter class is cleaned up after it plays');
});

test('a leaving row freezes its height, so the collapse has something to run from', () => {
  const clock = fakeTimers();
  const list = makeList();
  const tr = createFilterTransition({ list, reduced: () => false, setTimer: clock.setTimer, clearTimer: clock.clearTimer });
  renderKeys(list, tr, ['a']);
  renderKeys(list, tr, []);
  assert.equal(list.children[0].props['--leave-h'], '40px');
});

test('onLeave fires for every departing row (the popup stops measuring a ghost)', () => {
  const clock = fakeTimers();
  const list = makeList();
  const seen = [];
  const tr = createFilterTransition({
    list, reduced: () => false, setTimer: clock.setTimer, clearTimer: clock.clearTimer,
    onLeave: (el) => seen.push(el.dataset.key),
  });
  renderKeys(list, tr, ['a', 'b', 'c']);
  renderKeys(list, tr, ['b']);
  assert.deepEqual(seen, ['a', 'c']);
});

test('rapid filter changes never stack or strand: each pass clears the last one’s ghosts', () => {
  const clock = fakeTimers();
  const list = makeList();
  const tr = createFilterTransition({ list, reduced: () => false, setTimer: clock.setTimer, clearTimer: clock.clearTimer });

  // Fast typing in a search box: five renders inside one animation frame.
  renderKeys(list, tr, ['a', 'b', 'c', 'd']);
  renderKeys(list, tr, ['a', 'b', 'c']);
  renderKeys(list, tr, ['a', 'b']);
  renderKeys(list, tr, ['a']);
  renderKeys(list, tr, ['a', 'b', 'c', 'd']);   // …and backspaced again
  assert.equal(tr.ghostCount, 0, 'the last pass re-admitted everything, so nothing is leaving');
  assert.deepEqual(list.keys(), ['a', 'b', 'c', 'd'], 'the visible set is exactly the last one asked for');
  clock.advance(10_000);
  assert.deepEqual(list.keys(), ['a', 'b', 'c', 'd'], 'and no late timer takes a row away afterwards');
  assert.equal(clock.pendingCount, 0, 'no animation timer is left running');
});

test('a row that leaves and comes back mid-burst ends up visible, not a ghost', () => {
  const clock = fakeTimers();
  const list = makeList();
  const tr = createFilterTransition({ list, reduced: () => false, setTimer: clock.setTimer, clearTimer: clock.clearTimer });
  renderKeys(list, tr, ['a', 'b']);
  renderKeys(list, tr, ['a']);              // b starts leaving…
  assert.equal(tr.ghostCount, 1);
  renderKeys(list, tr, ['a', 'b']);         // …and is admitted again before it lands
  assert.equal(tr.ghostCount, 0, 'the stale ghost was dropped, not left under the fresh row');
  assert.deepEqual(list.keys(), ['a', 'b']);
  clock.advance(10_000);
  assert.deepEqual(list.keys(), ['a', 'b']);
});

test('reduced motion: no classes, no ghosts, straight to the filtered set', () => {
  const clock = fakeTimers();
  const list = makeList();
  const tr = createFilterTransition({ list, reduced: () => true, setTimer: clock.setTimer, clearTimer: clock.clearTimer });
  renderKeys(list, tr, ['a', 'b', 'c']);
  const diff = renderKeys(list, tr, ['b', 'd']);
  assert.deepEqual(diff, { entered: ['d'], left: ['a', 'c'] }, 'the diff is still reported');
  assert.deepEqual(list.keys(), ['b', 'd'], 'the correct result, immediately');
  assert.equal(tr.ghostCount, 0);
  assert.equal(clock.pendingCount, 0, 'no timers at all under reduced motion');
  assert.ok(list.children.every((el) => el.classes.size === 0), 'and no motion classes either');
});

test('skipEnter leaves a row the caller animates itself alone (the add flow’s materialize)', () => {
  const clock = fakeTimers();
  const list = makeList();
  const tr = createFilterTransition({ list, keyAttr: 'url', reduced: () => false, setTimer: clock.setTimer, clearTimer: clock.clearTimer });
  renderKeys(list, tr, ['https://a'], { attr: 'url' });
  renderKeys(list, tr, ['https://a', 'https://b'], { attr: 'url', skipEnter: ['https://b'] });
  const added = list.children.find((el) => el.dataset.url === 'https://b');
  assert.ok(!added.classList.contains(FILTER_IN_CLASS), 'materialize owns that row’s entrance');
});

test('rows without a key (an empty-state row) are ignored entirely', () => {
  const clock = fakeTimers();
  const list = makeList();
  const tr = createFilterTransition({ list, reduced: () => false, setTimer: clock.setTimer, clearTimer: clock.clearTimer });
  renderKeys(list, tr, ['a']);
  tr.begin();
  list.wipe();
  const empty = makeRow(undefined);
  delete empty.dataset.key;
  list.appendChild(empty);
  const diff = tr.end();
  assert.deepEqual(diff, { entered: [], left: ['a'] }, 'the placeholder is not counted as an arrival');
  assert.ok(!empty.classList.contains(FILTER_IN_CLASS));
  clock.advance(10_000);
  assert.deepEqual(list.children, [empty], 'and the ghost still leaves');
});

test('clear() drops every ghost at once, for a view going away mid-fade', () => {
  const clock = fakeTimers();
  const list = makeList();
  const tr = createFilterTransition({ list, reduced: () => false, setTimer: clock.setTimer, clearTimer: clock.clearTimer });
  renderKeys(list, tr, ['a', 'b']);
  clock.advance(FILTER_ENTER_MS + 100);   // let their entrances finish first
  renderKeys(list, tr, []);
  assert.equal(tr.ghostCount, 2);
  tr.clear();
  assert.equal(tr.ghostCount, 0);
  assert.deepEqual(list.keys(), []);
  assert.equal(clock.pendingCount, 0, 'their timers were cancelled, not left to fire');
});

// ── One row out, as a filter (not as a delete) ──────────────────────────────
test('filterLeave adds the light class and runs `done` when it lands', async () => {
  const row = el();
  const done = [];
  await filterLeave(row, () => done.push('ran'), { ms: 1, reduced: () => false });
  assert.deepEqual(done, ['ran']);
  assert.ok(row.has(FILTER_OUT_CLASS));
  assert.ok(!row.has('leaving'), 'a filter drop never plays the destructive leave');
});

test('filterLeave always runs `done` — reduced motion and a missing element included', async () => {
  const row = el();
  let ran = 0;
  await filterLeave(row, () => ran++, { reduced: () => true });
  assert.equal(ran, 1);
  assert.ok(!row.has(FILTER_OUT_CLASS), 'nothing to animate under reduced motion');
  await filterLeave(null, () => ran++);
  assert.equal(ran, 2);
});

test('animations/: a filter drop is lighter and quicker than a delete', () => {
  const out = css.match(/\.filter-out \{[\s\S]*?\n\}/)[0];
  const secs = (block, name) => parseFloat(block.match(new RegExp(`animation: ${name} ([\\d.]+)s`))[1]);
  const leaving = css.match(/\.leaving \{[\s\S]*?\n\}/)[0];
  assert.ok(secs(out, 'stFilterOut') < secs(leaving, 'stRowLeave'),
    'the filter fade must be quicker than the destructive collapse, or the two read alike');
  assert.ok(!/disintegrate|tile/i.test(out), 'no particles: a filter excluded the row, it was not destroyed');
  assert.match(css, /@keyframes stFilterOut \{[\s\S]*?to\s+\{ opacity: 0; max-height: 0/,
    'it collapses the row’s box, so the list closes the gap');
  assert.match(css, /\.filter-in \{ animation: stFilterIn/, 'and arrivals play the mirror of it');
  const reduced = css.slice(css.indexOf('@media (prefers-reduced-motion: reduce)'));
  assert.match(reduced, /\.filter-out \{ display: none !important; \}/,
    'reduced motion shows the final set, never a half-faded row');
});

// ── A chat entry ARRIVES as dust too (browser motion.js chatIn twin) ────────
// A removed message already had particles; an appearing one had none, which is what
// made the two directions read as different surfaces.
test('chatIn: veils at once, lifts only when the motes have landed', async () => {
  // One number owns both directions — the gather rides the clock the scatter falls on.
  // Shorter than a row's flight on purpose (browser twin): the motes carry no text, so a
  // long answer is unreadable until the veil lifts. The gather scales with it.
  // A SHARE of the row's flight, not a number of its own: the row's clock has been
  // shortened once already, and a fixed floor here would have the two meet.
  assert.ok(CHAT_ENTER_MS < DISINTEGRATE_MS && CHAT_ENTER_MS >= DISINTEGRATE_MS / 2,
    `chat arrival ${CHAT_ENTER_MS}ms of a ${DISINTEGRATE_MS}ms row flight`);
  const row = el();
  const p = chatIn(row);
  // SYNCHRONOUSLY veiled — before the caller returns, so no frame ever paints the entry
  // ahead of its own dust (fading it up underneath them showed the message first and
  // played the animation over it, which is the bug this replaced).
  assert.ok(row.has(CHAT_ENTERING_CLASS), 'veiled from the first frame');
  await p;
  // No DOM here, so no motes can fly — the veil must lift at once rather than hiding the
  // entry behind a flight that never happened.
  assert.ok(!row.has(CHAT_ENTERING_CLASS), 'never left stranded invisible');
  // Decoration only: a missing element must never make an append throw.
  await chatIn(null);

  const src = motionSrc();
  // The arriving cloud is hosted in the CALLER's `host` (assistant.js passes its
  // section), not in the transcript (its clones would read as live conversation to
  // everything that walks it) and not on <body> (every bubble rule here is scoped
  // `#sec-assistant .msg …`, so a body-level clone matched none of them and the motes
  // arrived as bare text with no fill or border).
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
  // The layer is position:fixed at the box measured when it launched, but a transcript
  // SCROLLS under it — the bottom-pin fires again on a 220ms timer, and a later turn
  // appends more rows. A cloud left where it started is drawn over whatever has since
  // moved into those coordinates: the reported "text appears mid-animation, breaking the
  // UI" (a user bubble's motes rendered on top of the error card below it).
  const src = motionSrc();
  assert.match(src, /const trackDust = \(el, ms, onDrop = \(\) => \{\}\) => \{/);
  // …and a subject that RESIZES mid-flight (a re-wrapped label, a font landing, the panel
  // dragged wider) leaves a cloud that no longer matches what arrives — there is no
  // re-photographing it, so the stale copy is dropped rather than shown at the wrong size.
  assert.match(src, /const resized = !r \|\| !shot \|\|/);
  assert.match(src, /if \(!rectInScroller\(r, s\) \|\| resized\) \{ cancelDust\(el\); live = false; onDrop\(\); return; \}/);
  // Each box is measured ONCE per frame and the host writes are batched after the
  // reads (skipping unchanged values) — the helpers re-measured per call, which forced
  // a layout per frame per flying cloud.
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
  // On the desktop the overlay is a real widget and paints only inside its own box, so a
  // mote can never reach the composer. Here the tiles translate freely out of an
  // `overflow: visible` host, and a gather next to the input rained motes across it
  // (reported). The clip is against the host's OWN border box, so the insets are signed:
  // negative EXPANDS it, letting a mote fly anywhere inside the transcript and nowhere
  // outside it.
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
  // The veil is lifted by a timer at the end of the FULL flight. A cancel that only killed
  // the motes therefore left the message invisible, with nothing standing in for it, until
  // that timer fired — up to the whole gather. Found by resizing a live entry mid-flight.
  const src = motionSrc();
  assert.match(src, /const trackDust = \(el, ms, onDrop = \(\) => \{\}\) => \{/);
  assert.match(src, /cancelDust\(el\); live = false; onDrop\(\); return;/);
  assert.match(src, /const stop = trackDust\(el, CHAT_ENTER_MS, handOver\);/);
  // …and exactly once, whichever path gets there first (a drop, or the flight ending).
  assert.match(src, /if \(handedOver\) return;/);
});

// ── No mote flies a straight line ───────────────────────────────────────────
// tileWaypoint itself is pinned to the app's (portParity.test.js); these are the
// extension's own carriers of it.
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

// ── The motion mode (lib/shellPrefs.js StencilMotion; browser motionPrefs.js twin) ──
test('the gates read StencilMotion live, and fall back to the OS preference without it', async () => {
  const m = await import('../src/lib/motion.js');
  const prev = globalThis.StencilMotion;
  try {
    delete globalThis.StencilMotion;
    assert.equal(m.motionMode(), 'particles');
    assert.equal(m.dustEnabled(), true, 'no script: the OS alone speaks, and it is fine with motion');
    assert.equal(m.particleStyle(), 'dust');
    assert.equal(m.wipeDurationMs(), Math.max(m.LEAVE_MS, m.DISINTEGRATE_MS));
    let mode = 'water';
    globalThis.StencilMotion = {
      get: () => mode,
      reduced: () => mode === 'none',
      particles: () => ['particles', 'water', 'fire'].includes(mode),
      style: () => ({ particles: 'dust', water: 'water', fire: 'fire' })[mode] || null,
    };
    assert.equal(m.motionMode(), 'water');
    assert.equal(m.dustEnabled(), true);
    assert.equal(m.particleStyle(), 'water');
    mode = 'slide';
    assert.equal(m.motionReduced(), false, 'slide still moves');
    assert.equal(m.dustEnabled(), false, '…just never out of particles');
    assert.equal(m.wipeDurationMs(), m.LEAVE_MS, 'a wipe is just the collapse then');
    // Every builder answers the gate: no cloud, no veil, the caller's own entrance.
    const el = { classList: new Set(), getBoundingClientRect: () => ({ left: 0, top: 0, width: 100, height: 20 }) };
    el.classList.add = (c) => Set.prototype.add.call(el.classList, c);
    assert.equal(m.disintegrate(el), false, 'disintegrate declines under slide');
    mode = 'none';
    assert.equal(m.motionReduced(), true);
    assert.equal(m.wipeDurationMs(), 0);
  } finally {
    if (prev === undefined) delete globalThis.StencilMotion; else globalThis.StencilMotion = prev;
  }
});

test('the mode is wired: the one cloud door, the chat slide, and the CSS half', () => {
  const src = motionSrc();
  const body = src.slice(src.indexOf('export function disintegrate('), src.indexOf('export const reintegrate'));
  assert.ok(body.includes('if (!dustEnabled()) return false;'), 'the cloud is built behind the gate');
  assert.ok(body.includes('const style = styleCode();') && body.includes('const paints = paletteCss();'),
            'a styled cloud is painted from the accent palette');
  assert.match(src, /if \(!dustEnabled\(\)\) \{ flashLanding\(el, CHAT_SLIDE_CLASS, CHAT_SLIDE_MS\); return Promise\.resolve\(\); \}/);
  // No helper still reads the media query by hand — motionReduced() is the one gate.
  assert.equal((src.match(/matchMedia\('\(prefers-reduced-motion: reduce\)'\)/g) || []).length, 1, 'only prefersReducedMotion itself');
  assert.match(css, /:root\[data-motion="none"\] \*,/);
  assert.match(css, /\.chat-slide-in \{ animation: chatRiseIn/);
  // Every extension page stamps the mode pre-paint through the accent script set.
  const prefs = readFileSync(new URL('../src/lib/prefs.js', import.meta.url), 'utf8');
  assert.match(prefs, /setAttribute\('data-motion'/);
});
