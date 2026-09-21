// createFilterAnimator (js/ui/motion.js): a filter is a question re-answered — render once
// and first, then play in the rows that are left.
import test from 'node:test';
import assert from 'node:assert';
import {
  createFilterAnimator, FILTER_ENTERING_CLASS, FILTER_ENTER_MS, tileWaypoint,
} from '../../js/ui/motion.js';
import { ANIMATIONS_CSS } from '../helpers/css.js';

// A filter is a question re-answered: render() runs exactly once per call and FIRST, and
// the effect belongs to the rows that are left.

test('filterDelta: what a change drops, reveals, and whether it merely moved', async () => {
  const { filterDelta } = await import('../../js/ui/motion.js');
  const d = filterDelta(['a', 'b', 'c'], ['b', 'd']);
  assert.deepEqual(d.leaving, ['a', 'c'], 'keys the new filter excludes');
  assert.deepEqual(d.entering, ['d'], 'keys it reveals');
  assert.ok(d.moved);
  // A sort switch: same membership, new order — nothing enters or leaves.
  const sorted = filterDelta(['a', 'b'], ['b', 'a']);
  assert.deepEqual(sorted.leaving, []);
  assert.deepEqual(sorted.entering, []);
  assert.ok(sorted.moved, 'but the list did move, so it is still worth playing');
  // An unchanged list must be recognisable as such (a keystroke that narrows nothing).
  const same = filterDelta(['a', 'b'], ['a', 'b']);
  assert.ok(!same.moved);
  assert.deepEqual(filterDelta(new Set(['a']), new Set(['a', 'b'])).entering, ['b'],
    'any iterable of keys, not just arrays');
});

// A tiny list rig: `state` is the key set the next render will show.
const filterRig = ({ shown = [], reduced = false } = {}) => {
  const timers = [];
  const rows = new Map();
  const rig = {
    renders: 0,
    keys: shown.slice(),
    nextKeys: shown.slice(),
    classesOf: (k) => rows.get(k)?.classes ?? new Set(),
    run: null,
    flush: () => { for (const t of timers.splice(0).sort((a, b) => a.ms - b.ms)) t.fn(); },
  };
  const makeRow = (k) => {
    const classes = new Set();
    return {
      classes,
      style: { setProperty(p, v) { this[p] = v; } },
      classList: { add: (...c) => c.forEach((x) => classes.add(x)), remove: (...c) => c.forEach((x) => classes.delete(x)) },
      getBoundingClientRect: () => ({ height: 40 }),
    };
  };
  for (const k of shown) rows.set(k, makeRow(k));
  rig.run = createFilterAnimator({
    keys: () => rig.keys,
    next: () => rig.nextKeys,
    // The rebuild: the shown set becomes the pending one, and every key gets a row.
    render: () => {
      rig.renders++;
      rig.keys = rig.nextKeys.slice();
      for (const k of rig.keys) if (!rows.has(k)) rows.set(k, makeRow(k));
    },
    find: (k) => rows.get(k) ?? null,
    setTimer: (fn, ms) => { timers.push({ fn, ms }); return timers.length; },
    reduced: () => reduced,
  });
  return rig;
};

test('a filter change re-renders FIRST, then plays the rows that are LEFT in', async () => {
  const rig = filterRig({ shown: ['a', 'b', 'c'] });
  rig.nextKeys = ['b', 'd'];
  await rig.run();
  assert.strictEqual(rig.renders, 1, 'exactly one rebuild, and nothing waited on it');
  assert.deepEqual(rig.keys, ['b', 'd'], 'the new answer is on screen at once');
  // Every row that is LEFT arrives — the filtered set is what changed, not just the
  // rows that happen to be new to it.
  assert.ok(rig.classesOf('d').has(FILTER_ENTERING_CLASS), 'the revealed row arrives');
  assert.ok(rig.classesOf('b').has(FILTER_ENTERING_CLASS), '…and so does the one that survived');
  rig.flush();
  assert.ok(!rig.classesOf('d').has(FILTER_ENTERING_CLASS), 'and the class is cleaned up after');
  assert.ok(!rig.classesOf('b').has(FILTER_ENTERING_CLASS));
});

test('what the filter DROPS never plays at all — it is simply not the answer any more', async () => {
  const rig = filterRig({ shown: ['a', 'b'] });
  rig.nextKeys = ['b'];
  await rig.run();
  const classes = rig.classesOf('a');
  assert.strictEqual(classes.size, 0, 'the excluded row is untouched: no exit to watch');
  assert.ok(!classes.has('leaving'), 'the destructive collapse belongs to a real removal');
  assert.ok(!classes.has('materializing'), 'and the dust gather to a real add');
  // A filter's SHAPE, not its speed, tells it from a removal: nothing plays on the way out,
  // so the arrival may be slow enough to read.
  assert.ok(FILTER_ENTER_MS >= 300, 'slow enough to read as a deliberate settle, not a flicker');
});

test('a filter that only REVEALS rows plays the whole set in too', async () => {
  const rig = filterRig({ shown: ['a'] });
  rig.nextKeys = ['a', 'b'];
  await rig.run();
  assert.strictEqual(rig.renders, 1);
  assert.ok(rig.classesOf('b').has(FILTER_ENTERING_CLASS));
  assert.ok(rig.classesOf('a').has(FILTER_ENTERING_CLASS));
});

test('a sort switch (same rows, new order) settles the whole list back in', async () => {
  const rig = filterRig({ shown: ['a', 'b'] });
  rig.nextKeys = ['b', 'a'];
  await rig.run();
  assert.strictEqual(rig.renders, 1);
  assert.ok(rig.classesOf('a').has(FILTER_ENTERING_CLASS), 're-sorted rows read as arriving');
  assert.ok(rig.classesOf('b').has(FILTER_ENTERING_CLASS));
});

test('a change that moves nothing just renders — no flash', async () => {
  const rig = filterRig({ shown: ['a', 'b'] });
  rig.nextKeys = ['a', 'b'];
  await rig.run();
  assert.strictEqual(rig.renders, 1, 'the rebuild still happens (the row CONTENT may differ)');
  assert.ok(!rig.classesOf('a').has(FILTER_ENTERING_CLASS));
  assert.ok(!rig.classesOf('b').has(FILTER_ENTERING_CLASS));
});

test('reduced motion: straight to the re-render, no classes at all', async () => {
  const rig = filterRig({ shown: ['a', 'b'], reduced: true });
  rig.nextKeys = ['b'];
  await rig.run();
  assert.strictEqual(rig.renders, 1);
  assert.deepEqual(rig.keys, ['b'], 'and the right set is on screen');
  assert.strictEqual(rig.classesOf('a').size, 0);
  assert.strictEqual(rig.classesOf('b').size, 0);
});

test('fast typing: every keystroke renders NOW, and never drops rows', async () => {
  const rig = filterRig({ shown: ['a', 'b', 'c'] });
  rig.nextKeys = ['a', 'b'];        // keystroke 1
  await rig.run();
  assert.strictEqual(rig.renders, 1, 'on screen immediately — there is no exit to wait on');
  rig.nextKeys = ['a'];             // keystroke 2, hard on its heels
  await rig.run();
  assert.strictEqual(rig.renders, 2, 'and so is the next one');
  assert.deepEqual(rig.keys, ['a'], 'the final rendered set is the last one asked for');
  rig.flush();                      // the enter clean-ups fire late, over nothing
  assert.deepEqual(rig.keys, ['a']);
});

test('animations.css: a filter has an arrival and no exit at all', () => {
  const css = ANIMATIONS_CSS;
  assert.ok(!/\.filter-leaving/.test(css), 'nothing plays a filtered-out row out any more');
  assert.ok(!/rowFilterOut/.test(css), '…and its keyframes are gone with it');
  assert.match(css, /\.filter-entering \{\s*animation: rowFilterIn 0\.34s/);
  assert.match(css, /@keyframes rowFilterIn \{\s*from \{ opacity: 0; transform: translateY\(-4px\); \}/,
    'the rows that are left arrive — that is the whole effect');
  assert.match(css, /@media \(prefers-reduced-motion: reduce\) \{\s*\.filter-entering \{ animation: none; \}/);
});

// Every flight bends through an off-line waypoint (tileWaypoint), so a cloud churns instead
// of radiating in spokes — the same recipe on every surface.
