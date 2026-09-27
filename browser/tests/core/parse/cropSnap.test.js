// Committing a crop (js/core/parse/cropGeometry.js snapCropRectJS / rotateEditQuarterJS): the
// Math.round snap ImageModel.roundRect runs, and one quarter-turn of the whole edit. Core twin:
// core/tests/geometry/cropSnap.test.cpp.
import test from 'node:test';
import assert from 'node:assert/strict';

import { snapCropRectJS, rotateEditQuarterJS, rotateEditQuarter } from '../../../js/core/parse/cropGeometry.js';

test('snapCropRect rounds each side into the image, then moves the origin inside', () => {
  assert.deepEqual(snapCropRectJS({ x: -5, y: -5, width: 999, height: 999 }, 200, 100), { x: 0, y: 0, width: 200, height: 100 });
  assert.deepEqual(snapCropRectJS({ x: 190.4, y: 2.6, width: 20.5, height: 10.2 }, 200, 100), { x: 179, y: 3, width: 21, height: 10 });
  assert.deepEqual(snapCropRectJS({ x: 10.5, y: 0, width: 0.2, height: 2.5 }, 200, 100), { x: 11, y: 0, width: 1, height: 3 },
    'halves round up, and a side never drops below 1px');
  assert.deepEqual(snapCropRectJS({ x: 3, y: 3, width: 9, height: 9 }, 0.5, 3), { x: 0, y: 0, width: 1, height: 3 },
    'an image narrower than a pixel still gets a 1px window at the origin');
});

test('rotateEditQuarter carries the window into the turned space and wraps the count', () => {
  const r = { x: 10, y: 20, width: 80, height: 40 };
  assert.deepEqual(rotateEditQuarterJS(r, 0, 200, 100, true), { crop: { x: 40, y: 10, width: 40, height: 80 }, quarters: 1 });
  assert.deepEqual(rotateEditQuarterJS(r, 0, 200, 100, false), { crop: { x: 20, y: 110, width: 40, height: 80 }, quarters: 3 });
  assert.deepEqual(rotateEditQuarterJS({ x: 5, y: 10, width: 40, height: 80 }, 1, 200, 100, true),
    { crop: { x: 110, y: 5, width: 80, height: 40 }, quarters: 2 }, 'an odd count reads the original swapped');
  let turn = { crop: r, quarters: 0 };
  for (let i = 0; i < 4; i++) turn = rotateEditQuarterJS(turn.crop, turn.quarters, 200, 100, true);
  assert.deepEqual(turn, { crop: r, quarters: 0 }, 'four turns come back');
});

test('rotateEditQuarter snaps a fractional window after the turn', () => {
  assert.deepEqual(rotateEditQuarterJS({ x: 0.4, y: 7.6, width: 33.5, height: 90 }, 0, 200, 100, true),
    { crop: { x: 2, y: 0, width: 90, height: 34 }, quarters: 1 });
});

test('rotateEditQuarter turns the crop-local lines inside the OLD window', () => {
  const lines = [{ points: [{ x: 0, y: 0 }, { x: 80, y: 40 }] }];
  rotateEditQuarter(lines, { x: 10, y: 20, width: 80, height: 40 }, 0, 200, 100, true);
  assert.deepEqual(lines[0].points, [{ x: 40, y: 0 }, { x: 0, y: 80 }]);
});
