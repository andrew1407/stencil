import test from 'node:test';
import assert from 'node:assert';
import { createTrailingSave, ZOOM_SAVE_DEBOUNCE_MS } from '../../../js/core/zoom/pan.js';
import { installViewport } from '../../helpers/zoomViewportRig.js';

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
test('every zoom persist route rides the debounce', () => {
  const { app, zp } = installViewport();
  let saves = 0;
  app.storage.save = () => { saves++; };
  for (const s of [1.2, 1.4, 1.6]) zp.setZoom(s);
  assert.strictEqual(saves, 0, 'setZoom (wheel/hold steps) never saves directly');
  assert.strictEqual(zp.persistZoom.pending(), true, 'it arms the trailing save');
  zp.persistZoom.flush();
  assert.strictEqual(saves, 1, 'the burst writes once');
  zp.zoomAroundCenter(2);
  assert.strictEqual(saves, 1, 'the animated zoom never saves directly either');
  assert.strictEqual(zp.persistZoom.pending(), true, 'its final snap arms the same trailing save');
  zp.persistZoom.flush();
  assert.strictEqual(saves, 2);
  zp.setZoom(1, false);
  assert.strictEqual(zp.persistZoom.pending(), false, 'a non-persisting zoom arms nothing');
});
