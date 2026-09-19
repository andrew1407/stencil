// createFilterTransition and filterLeave (src/lib/motion.js): every filtered list is rebuilt
// wholesale, and this is what makes that read symmetrically. DOM and clock are listDom.js stubs.
import test from 'node:test';
import assert from 'node:assert';
import { animationsCss } from './helpers/sources.js';
import {
  diffListKeys, createFilterTransition, filterLeave,
  FILTER_IN_CLASS, FILTER_OUT_CLASS, FILTER_ENTER_MS, FILTER_LEAVE_MS,
} from '../src/lib/motion.js';
import { classEl as el, makeList, makeRow, renderKeys, fakeTimers } from './helpers/listDom.js';

const css = animationsCss();

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
