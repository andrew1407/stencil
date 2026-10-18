// The matrix rain (js/ui/cli/skin/rain.js): drops fall a row a frame, trail 14, a fresh one past the
// bottom; 90 frames then done, the scrollback under it untouched; nothing to rain on draws nothing.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubDocument, createStubElement } from '../../../helpers/dom.js';
import { stepHeads, rain } from '../../../../js/ui/cli/skin/rain.js';

test('heads advance a row a frame and start afresh past the trail', () => {
  assert.deepEqual(stepHeads([0, 5], 10, () => 0.5), [1, 6]);
  assert.deepEqual(stepHeads([24], 10, () => 0.5), [-5], 'past h + trail: a new drop from above');
});

test('the rain runs its frames over the hidden scrollback, then clears its own rows; no rows means nothing', () => {
  const doc = createStubDocument();
  const pre = createStubElement('pre', { ownerDocument: doc });
  pre.querySelectorAll = (sel) => pre.children.filter((c) => c.className === sel.slice(1));
  const line = createStubElement('span', { className: 'console-line' });
  pre.appendChild(line);
  const timers = [];
  let done = 0;
  rain(pre, { cols: 4, rows: 3, random: () => 0.3, schedule: (fn, ms) => { timers.push([fn, ms]); return timers.length; }, onDone: () => done++ });
  assert.equal(timers[0][1], 30);
  assert.equal(pre.dataset.raining, '1', 'the lines hide under the rain');
  let frames = 0;
  while (timers.length) {
    timers.shift()[0]();
    frames++;
    if (timers.length) assert.equal(pre.children.filter((c) => c.className === 'cli-rain-row').length, 3, 'one row span per output row, repainted');
  }
  assert.equal(frames, 90);
  assert.equal(done, 1);
  assert.deepEqual(pre.children, [line], 'the rain is gone, the scrollback stays');
  assert.equal(pre.dataset.raining, undefined);
  rain(pre, { cols: 4, rows: 0, onDone: () => done++ });
  assert.equal(done, 2);
});
