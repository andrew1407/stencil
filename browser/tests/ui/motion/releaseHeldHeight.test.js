// js/ui/motion/easeBoxHeight.js releaseHeldHeight: a list held at its old height through a removal
// (the projects and servers windows) eases down to what it now needs — its window following —
// instead of dropping there in one frame (user report).
import test from 'node:test';
import assert from 'node:assert';

import { setMotionPrefs } from '../../../js/ui/motion/motionPrefs.js';

globalThis.ResizeObserver = class { observe() {} disconnect() {} };
const { releaseHeldHeight, BOX_RESIZE_MS, BOX_RESIZE_EASE } = await import('../../../js/ui/motion/easeBoxHeight.js');

// A list that measures `natural` once nothing holds it; `animate` records its one flight.
const makeList = (natural) => {
  const list = {
    style: { minHeight: '' },
    flight: null,
    getBoundingClientRect: () => ({ height: list.style.minHeight ? parseFloat(list.style.minHeight) : natural }),
    animate(frames, opts) {
      let settle;
      const f = { frames, opts, cancelled: false, finished: new Promise((r) => { settle = r; }) };
      f.settle = settle;
      f.cancel = () => { f.cancelled = true; };
      list.flight = f;
      return f;
    },
  };
  return list;
};

test('a held list eases from its held height to its natural one, then lets go', async () => {
  setMotionPrefs({ mode: 'particles' });
  const list = makeList(120);
  list.style.minHeight = '600px';
  releaseHeldHeight(list, 600);
  const f = list.flight;
  assert.deepStrictEqual(f.frames, [{ minHeight: '600px' }, { minHeight: '120px' }]);
  assert.deepStrictEqual([f.opts.duration, f.opts.easing, f.opts.fill], [BOX_RESIZE_MS, BOX_RESIZE_EASE, 'forwards']);
  assert.equal(list.style.minHeight, '600px', 'pinned until the flight takes its first frame');
  f.settle();
  await f.finished;
  await Promise.resolve();
  assert.ok(f.cancelled, 'the finished flight comes off…');
  assert.equal(list.style.minHeight, '', '…and the list is free again');
});

test('a new removal mid-flight cancels it and takes its own hold', () => {
  setMotionPrefs({ mode: 'particles' });
  const list = makeList(120);
  const letGo = releaseHeldHeight(list, 600);
  letGo();
  assert.ok(list.flight.cancelled);
  assert.equal(list.style.minHeight, '');
});

test('nothing flies when the list needs as much, or under motion none', () => {
  setMotionPrefs({ mode: 'particles' });
  const same = makeList(600);
  releaseHeldHeight(same, 600);
  assert.equal(same.flight, null);
  assert.equal(same.style.minHeight, '');
  setMotionPrefs({ mode: 'none' });
  const still = makeList(120);
  releaseHeldHeight(still, 600);
  assert.equal(still.flight, null, 'the height lands at once');
  assert.equal(still.style.minHeight, '');
});
