// A press just outside the picture (core/pointer/canvasCoords.js) lands on its edge: image px stay
// inside [0, width] × [0, height], so a point placed at the border never reads -1 px or -0.01 cm.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { canvasCoords } from '../../../js/core/pointer/canvasCoords.js';

const app = () => ({
  scale: 1,
  canvas: { width: 400, height: 300, style: {}, getBoundingClientRect: () => ({ left: 10, top: 20, width: 200, height: 150 }) },
});

test('inside the picture the mapping is the plain css → image scale', () => {
  const { x, y, cssX, cssY } = canvasCoords(app(), 60, 70);
  assert.deepEqual([x, y, cssX, cssY], [100, 100, 50, 50]);
});

test('past any edge the image px clamp to it; the css px do not', () => {
  const low = canvasCoords(app(), 9.5, 19);
  assert.deepEqual([low.x, low.y], [0, 0]);
  assert.deepEqual([low.cssX, low.cssY], [-0.5, -1], 'the zoom rect and divider still read the raw offset');
  const high = canvasCoords(app(), 300, 400);
  assert.deepEqual([high.x, high.y], [400, 300]);
});
