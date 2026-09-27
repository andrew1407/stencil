// The mark's veil and its wiring (js/ui/control/swap.js, customSelect.js): one delegated
// listener per checkbox, a word exchanged only on a real change, filtered rows in as sand —
// each driven on a stub page where the clouds are really built (helpers/dustCloudRig.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';
import { installDustDom, rect, boxEl, cloudKind } from '../../helpers/dustCloudRig.js';
import { element } from '../../helpers/visualsModalRig.js';
import { ANIMATIONS_CSS } from '../../helpers/css.js';

const dust = installDustDom({
  docOpts: { createElement: element },
  globals: {
    window: { innerWidth: 1200, innerHeight: 800, addEventListener() {}, removeEventListener() {} },
    HTMLSelectElement: class { get value() { return this._value; } set value(v) { this._value = v; } },
  },
});

const {
  MARK_FORMING_CLASS, FILTER_DUST_MS, FILTER_DUST_DRIFT, FILTER_ENTER_MS, markIn, markOut,
  markSwap, revealControls, settleMark, filterDust, createFilterAnimator, scatterGridFor, SCATTER_MAX_ROWS,
} = await import('../../../js/ui/motion.js');
const { installControlSwap, setChecked } = await import('../../../js/ui/control/swap.js');
const { enhanceSelect } = await import('../../../js/ui/control/customSelect.js');
const animCss = ANIMATIONS_CSS;

// A checkbox whose paint follows its state; `pokes` logs every write to `checked`.
const box = (type = 'checkbox', checked = false) => {
  const el = boxEl(rect(10, 10, 15, 15), { type });
  let on = checked;
  el.pokes = [];
  Object.defineProperty(el, 'checked', { get: () => on, set: (v) => { el.pokes.push(v); on = v; } });
  return el;
};
const paintOf = (el) => ({ backgroundColor: el.checked && !el.inkless ? 'rgb(124, 58, 237)' : 'rgba(0, 0, 0, 0)',
                           borderTopColor: 'rgb(9, 9, 9)' });

test('the veil hides the MARK, never the control around it', () => {
  assert.equal(MARK_FORMING_CLASS, 'mark-forming');
  const rule = animCss.slice(animCss.indexOf('.mark-forming {'));
  assert.match(rule, /@keyframes markForm \{ 0%, 6\d% \{ opacity: 0; \} 100% \{ opacity: 1; \} \}/,
    'the real mark waits until the motes have very nearly landed');
  // A checkbox keeps its outline throughout: only the accent fill and the tick are the
  // mark, so the veil suppresses those instead of the whole control's opacity.
  const box = rule.slice(rule.indexOf('input[type=checkbox].mark-forming'));
  const body = box.slice(0, box.indexOf('}'));
  assert.match(body, /animation: none;/, 'no opacity fade — that would blink the border');
  assert.match(body, /background-color: transparent;/);
  assert.match(body, /background-image: none;/);
  assert.match(animCss.slice(animCss.indexOf('.mark-forming {')),
    /@media \(prefers-reduced-motion: reduce\) \{\s*\n\s*\.mark-forming \{ animation: none; \}/);
});

test('one delegated listener wires every checkbox, as one filter does on the desktop', () => {
  globalThis.getComputedStyle = paintOf;
  const root = createStubElement('div');
  installControlSwap(root);
  installControlSwap(root);
  assert.equal(root.listeners.change.length, 1, 'one listener, however often it is installed');
  // A click on the box or its label: a tick gathers, an untick falls.
  const tick = box('checkbox', true);
  root.dispatch('change', { target: tick });
  assert.equal(cloudKind(tick), 'dust-forming');
  assert.deepEqual(tick.pokes, [], 'a ticked box is read as it stands…');
  tick.checked = false;
  tick.pokes.length = 0;
  root.dispatch('change', { target: tick });
  assert.equal(cloudKind(tick), 'dust-falling');
  assert.deepEqual(tick.pokes, [], '…and cached: an unticked one paints nothing to read');
  // Unseen and unticked, the look is probed by ticking it for one synchronous read.
  const fresh = box('checkbox', false);
  root.dispatch('change', { target: fresh });
  assert.deepEqual(fresh.pokes, [true, false]);
  assert.equal(cloudKind(fresh), 'dust-falling');
  // A radio cannot be poked for a reading — ticking one unticks its group.
  const radio = box('radio', false);
  root.dispatch('change', { target: radio });
  assert.deepEqual(radio.pokes, []);
  assert.equal(cloudKind(radio), 'dust-falling', 'its ring is the reading instead');
  const pill = box('checkbox', false);
  pill.inkless = true;
  root.dispatch('change', { target: pill });
  assert.equal(cloudKind(pill), null, 'an indicator that paints nothing has nothing to scatter');
  root.dispatch('change', { target: boxEl(rect(0, 0, 40, 20), { type: 'text' }) });
  // …and a programmatic set animates too, but only a real change.
  const set = box('checkbox', false);
  setChecked(set, false);
  assert.deepEqual(set.pokes, [], 'a set to the state it already has is not a change to show');
  setChecked(set, true);
  assert.equal(set.checked, true);
  assert.equal(cloudKind(set), 'dust-forming');
});

test('a select exchanges its chosen word, but only on a real change', () => {
  const pick = () => {
    const host = element('div');
    const select = host.appendChild(element('select'));
    Object.assign(select, { _value: 'a', options: [{ value: 'a', textContent: 'A' }, { value: 'b', textContent: 'B' }] });
    enhanceSelect(select);
    const trigger = select.parentNode.children.at(-2);
    const cur = trigger.querySelector('.cs-cur');
    cur.getBoundingClientRect = () => rect(0, 0, 60, 18);
    return { select, cur, trigger };
  };
  dust.reset();
  const first = pick();
  assert.equal(first.cur.textContent, 'A', 'the first paint writes straight through');
  // BOOT: a restored value set in the task that wired the control is still the first paint.
  first.select.value = 'b';
  assert.equal(first.cur.textContent, 'B');
  assert.equal(first.cur.__dustHost ?? null, null, 'no flight before the first frame');
  dust.frame();
  first.select.value = 'a';
  assert.equal(first.cur.textContent, 'A');
  assert.equal(cloudKind(first.cur), 'dust-forming', 'a settled change swaps the word as dust');
  assert.ok(dust.clouds().some((h) => h.className.endsWith('dust-falling')), 'the old word falls away');
  const layers = dust.clouds().length;
  first.select.value = 'a';
  first.trigger.dispatch('click', { preventDefault() {} });
  assert.equal(dust.clouds().length, layers, 'a re-sync on open, or a no-op set, flies nothing');
  // Off-browser there is no first frame to wait for: every set is a real one.
  const raf = globalThis.requestAnimationFrame;
  delete globalThis.requestAnimationFrame;
  try {
    const bare = pick();
    bare.select.value = 'b';
    assert.equal(cloudKind(bare.cur), 'dust-forming');
  } finally { globalThis.requestAnimationFrame = raf; }
});

test('a filter brings its rows in as sand — and never plays one out', () => {
  assert.ok(FILTER_DUST_MS <= FILTER_ENTER_MS * 2,
    'a view change has to keep up with typing in a search box');
  assert.ok(FILTER_DUST_DRIFT < 1, 'and it must never read as the delete it is not');
  dust.reset();
  const row = () => boxEl(rect(0, 0, 700, 40));
  const alone = row();
  assert.equal(filterDust(alone, 0, 1), true);
  // One direction only: what a filter drops was never destroyed, so it does not come apart.
  assert.equal(cloudKind(alone), 'dust-forming');
  assert.equal(alone.__dustHost.__cloud.flight, 'gather');
  assert.equal(alone.__dustHost.style['--dust-ms'], `${FILTER_DUST_MS}ms`);
  assert.equal(alone.__dustHost.children.length, 0, 'anonymous specks, never copies of a row');
  // One shared mesh budget across every row the change moves…
  const crowd = row();
  filterDust(crowd, 0, SCATTER_MAX_ROWS);
  assert.ok(crowd.__dustHost.__cloud.motes.length < alone.__dustHost.__cloud.motes.length / 2);
  assert.ok(scatterGridFor(SCATTER_MAX_ROWS, 0).cols > 0);
  // …and past the row ceiling a row simply fades, as it always did.
  assert.equal(filterDust(row(), SCATTER_MAX_ROWS, SCATTER_MAX_ROWS + 1), false);
  // The animator measures every row once, then hands each its box and its share.
  const reads = new Map();
  const rows = ['a', 'b', 'c'].map((k) => {
    const el = boxEl(() => { reads.set(k, (reads.get(k) || 0) + 1); return rect(0, 0, 700, 40); });
    return [k, el];
  });
  const find = (k) => rows.find(([key]) => key === k)[1];
  createFilterAnimator({ keys: () => ['a'], next: () => ['a', 'b', 'c'], find, setTimer: () => 0, reduced: () => false })();
  assert.deepEqual([...reads.values()], [1, 1, 1], 'one read pass: each row is measured once');
  for (const [, el] of rows) assert.equal(cloudKind(el), 'dust-forming');
});

test('nothing here throws off-browser, or on a stub — decoration is never load-bearing', () => {
  for (const fn of [markIn, markOut, settleMark])
    assert.doesNotThrow(() => fn(null));
  assert.equal(markIn(null), false);
  assert.equal(markOut(undefined), false);
  assert.equal(markSwap({}, null), false, 'no writer, no swap');
  assert.equal(revealControls(null, true), false);
  assert.equal(filterDust(null, true), false);
  // A swap on a stub still WRITES the value — the DOM is never behind the state.
  let wrote = 0;
  markSwap({ getBoundingClientRect: () => ({ width: 0, height: 0 }) }, () => { wrote++; });
  assert.equal(wrote, 1);
});
