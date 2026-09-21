import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import { createTrailingSave, ZOOM_SAVE_DEBOUNCE_MS } from '../js/core/zoom/pan.js';

// The zoom persistence debounce. Every wheel tick / hold-repeat used to call
// storage.save() directly — a full layout + thumbnail write per notch, each raising
// its own "Saved" toast, so one zoom burst stacked a flicker of them. The trailing
// window collapses the burst: only the final zoom level saves, once, and the toast
// fires once (and coalesces besides — see notifications.test.js).

const stubTimers = () => {
  let seq = 0;
  const armed = new Map();
  return {
    setTimer: (fn, ms) => { const id = ++seq; armed.set(id, { fn, ms }); return id; },
    clearTimer: (id) => armed.delete(id),
    fire: () => { const due = [...armed.values()]; armed.clear(); due.forEach(({ fn }) => fn()); },
    get armedCount() { return armed.size; },
    get lastDelay() { return [...armed.values()].at(-1)?.ms; },
  };
};

test('a zoom burst collapses into ONE trailing save', () => {
  const t = stubTimers();
  let saves = 0;
  const persist = createTrailingSave(() => saves++, { setTimer: t.setTimer, clearTimer: t.clearTimer });
  for (let i = 0; i < 12; i++) persist();     // a wheel burst — twelve notches
  assert.strictEqual(saves, 0, 'nothing saves mid-burst');
  assert.strictEqual(t.armedCount, 1, 'every step re-arms the ONE trailing timer');
  assert.strictEqual(t.lastDelay, ZOOM_SAVE_DEBOUNCE_MS, 'armed on the shared window');
  assert.strictEqual(persist.pending(), true);
  t.fire();
  assert.strictEqual(saves, 1, 'the burst saves once, at its end');
  assert.strictEqual(persist.pending(), false);
});

test('a fresh step after the save starts a new cycle', () => {
  const t = stubTimers();
  let saves = 0;
  const persist = createTrailingSave(() => saves++, { setTimer: t.setTimer, clearTimer: t.clearTimer });
  persist();
  t.fire();
  persist();                                   // zooming again later
  t.fire();
  assert.strictEqual(saves, 2, 'each quiet burst persists exactly once');
});

test('flush runs a pending save NOW, once — and is a no-op when idle', () => {
  const t = stubTimers();
  let saves = 0;
  const persist = createTrailingSave(() => saves++, { setTimer: t.setTimer, clearTimer: t.clearTimer });
  persist.flush();
  assert.strictEqual(saves, 0, 'nothing pending, nothing to flush');
  persist();
  persist.flush();
  assert.strictEqual(saves, 1, 'the pending save ran immediately');
  assert.strictEqual(t.armedCount, 0, 'and its timer was disarmed');
  t.fire();
  assert.strictEqual(saves, 1, 'no ghost save from the flushed cycle');
});

// Both halves: pan.js owns setZoom, animation.js the animated zoom's final snap.
test('every zoom persist route rides the debounce (source pin)', () => {
  const pan = readFileSync(new URL('../js/core/zoom/pan.js', import.meta.url), 'utf8');
  const anim = readFileSync(new URL('../js/core/zoom/animation.js', import.meta.url), 'utf8');
  const src = pan + anim;
  assert.strictEqual((src.match(/storage\.save\(\)/g) || []).length, 1,
    'exactly one direct storage save call — the one inside the trailing runner');
  assert.match(pan, /if \(persist && this\.app\.image\) this\.persistZoom\(\);/,
    'setZoom (wheel/hold steps) routes through the debounce');
  assert.strictEqual((src.match(/persistZoom\(\);/g) || []).length, 2,
    'both routes — setZoom and the animated zoom’s final snap');
});
