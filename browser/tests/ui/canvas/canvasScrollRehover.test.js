// A pan or scroll under a still cursor re-runs the hover where the cursor rests
// (ui/bindings/canvasPointer.js), so the tooltip never lingers over what used to be there.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../helpers/dom.js';

const setup = async (over) => {
  const canvas = createStubElement('canvas');
  const viewport = createStubElement('div');
  const doc = installDom();
  doc.register('canvas-viewport', viewport);
  const calls = { hide: 0, status: 0 };
  const app = {
    canvas, image: null, mouseOverCanvas: true, mouseLeftAt: 0, lastMouseClientX: 40, lastMouseClientY: 50,
    tooltip: { hide() { calls.hide++; } },
    updateCoordStatus() { calls.status++; },
    renderer: { redraw() {} },
    ...over,
  };
  const { wireCanvasPointer } = await import('../../../js/ui/bindings/canvasPointer.js');
  wireCanvasPointer(app);
  return { app, viewport, calls };
};

test('a scroll with the cursor over the canvas re-hovers at its last position', async () => {
  const { viewport, calls } = await setup();
  assert.ok(viewport.listeners.scroll?.length, 'the viewport listens for scroll');
  viewport.listeners.scroll[0]({ timeStamp: 10 });
  assert.equal(calls.hide, 1, 'the stale tooltip is taken down (or re-aimed) at once');
  assert.equal(calls.status, 1, 'and the readout follows the new point under the cursor');
});

test('a scroll with the cursor away from the canvas leaves the hover alone', async () => {
  const { viewport, calls } = await setup({ mouseOverCanvas: false });
  viewport.listeners.scroll[0]({ timeStamp: 10 });
  assert.deepEqual(calls, { hide: 0, status: 0 });
});
