import { test } from 'node:test';
import assert from 'node:assert/strict';
import { classifyEnd, isLongPress, dist, midpoint, touchDist, TOUCH_DEFAULTS } from '../js/core/touchGestures.js';

const T = (x, y) => ({ clientX: x, clientY: y });

test('classifyEnd: short & still → tap', () => {
  assert.equal(classifyEnd({ moved: 0, elapsed: 50 }), 'tap');
  assert.equal(classifyEnd({ moved: TOUCH_DEFAULTS.moveTol, elapsed: TOUCH_DEFAULTS.tapMaxMs }), 'tap');
});

test('classifyEnd: moved too far → drag', () => {
  assert.equal(classifyEnd({ moved: TOUCH_DEFAULTS.moveTol + 1, elapsed: 50 }), 'drag');
});

test('classifyEnd: held too long → drag (became a hold, not a tap)', () => {
  assert.equal(classifyEnd({ moved: 0, elapsed: TOUCH_DEFAULTS.tapMaxMs + 1 }), 'drag');
});

test('classifyEnd: custom thresholds override defaults', () => {
  assert.equal(classifyEnd({ moved: 20, elapsed: 50 }, { moveTol: 30 }), 'tap');
});

test('isLongPress: still & long enough → true; moved → false', () => {
  assert.equal(isLongPress({ moved: 2, elapsed: TOUCH_DEFAULTS.longPressMs }), true);
  assert.equal(isLongPress({ moved: 2, elapsed: TOUCH_DEFAULTS.longPressMs - 1 }), false);
  assert.equal(isLongPress({ moved: TOUCH_DEFAULTS.moveTol + 1, elapsed: 9999 }), false);
});

test('dist / touchDist: 3-4-5 triangle', () => {
  assert.equal(dist(0, 0, 3, 4), 5);
  assert.equal(touchDist(T(0, 0), T(3, 4)), 5);
});

test('midpoint: averages both axes', () => {
  assert.deepEqual(midpoint(T(0, 0), T(10, 20)), { x: 5, y: 10 });
});
