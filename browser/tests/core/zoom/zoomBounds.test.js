// The zoom range's one JS home, core/zoom/pan.js: its fallback bounds against the C++ constants,
// the clamp over them, and the percent range the toolbar's zoom input is built with. Node never
// loads wasm, so this is the drift check that runs everywhere; tests/wasm/ adds the live core.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { ZoomPan, rectZoom, zoomMin, zoomMax, zoomPercentBounds } from '../../../js/core/zoom/pan.js';
import { layout } from '../../../js/ui/layout.js';

const HEADER = readFileSync(new URL('../../../../core/state/zoomPan.hpp', import.meta.url), 'utf8');
const constant = (name) => Number(HEADER.match(new RegExp(`constexpr double ${name} = ([0-9.]+);`))[1]);

test('the JS fallback bounds are core/state/zoomPan.hpp ZOOM_MIN / ZOOM_MAX', () => {
  assert.equal(zoomMin(), constant('ZOOM_MIN'));
  assert.equal(zoomMax(), constant('ZOOM_MAX'));
});

test('the fallback clampScale clamps into [zoomMin(), zoomMax()]', () => {
  const clamp = new ZoomPan({}).clampScale;
  assert.equal(clamp(-3), zoomMin());
  assert.equal(clamp(0.001), zoomMin());
  assert.equal(clamp(1.5), 1.5);
  assert.equal(clamp(99), zoomMax());
});

test('the zoom input takes the range in whole percents, 5–3200', () => {
  assert.deepEqual(zoomPercentBounds(), { min: 5, max: 3200 });
  assert.ok(layout().includes('id="zoom-input" value="100" min="5" max="3200" autocomplete="off"'));
});

test('rectZoom fills and centres the swept rect, capped at zoomMax() as core/state/zoomPan.cpp is', () => {
  assert.deepEqual(rectZoom(50, 50, 100, 100, 400, 400), { scale: 4, scrollLeft: 200, scrollTop: 200 });
  assert.equal(rectZoom(0, 0, 10, 10, 400, 400).scale, zoomMax(), 'a tiny rect stops at the core ceiling, not 5x');
  const wide = rectZoom(0, 0, 8000, 100, 400, 400);
  assert.equal(wide.scale, 0.05, 'the narrower axis wins, floored at zoomMin()');
  assert.deepEqual([wide.scrollLeft, wide.scrollTop], [0, 0], 'never a negative scroll');
});
