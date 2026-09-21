// Clicking the canvas takes keyboard focus (ui/bindings/canvasPointer.js). The chat panel
// focuses its textarea on open and kept it, so a bare Delete on a just-clicked line edited chat
// text instead. DOM-stub idiom from coordTableDeleteKey.test.js: capture the real listeners.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from './helpers/dom.js';

const setup = async () => {
  const canvas = createStubElement('canvas');
  const viewport = createStubElement('div');
  const doc = installDom();
  doc.register('canvas-viewport', viewport);
  doc.activeElement = { id: 'chat-input' };  // the chat textarea, as after chat.open()
  const app = {
    canvas,
    canvasClick() {}, canvasDblClick() {}, canvasMouseMove() {},
    deselectEmptyArea() {},
    tooltipMgr: { hide() {} },
    updateCoordStatus() {},
    renderer: { redraw() {} },
  };
  const { wireCanvasPointer } = await import('../js/ui/bindings/canvasPointer.js');
  wireCanvasPointer(app);
  return { canvas, viewport };
};

test('the viewport focuses the canvas on pointerdown, so keys reach the drawing', async () => {
  const { canvas, viewport } = await setup();

  // pointerdown, not mousedown: must work for pen and touch too.
  assert.ok(viewport.listeners.pointerdown?.length,
    'the viewport must listen for pointerdown to move focus');

  viewport.listeners.pointerdown[0]({ target: canvas });
  assert.equal(canvas.focusCalls.length, 1, 'the canvas is focused on press');
  assert.deepEqual(canvas.focusCalls[0], { preventScroll: true },
    'focus must not scroll the viewport out from under the in-flight gesture');
});

test('re-focusing is skipped when the canvas already has focus', async () => {
  const { canvas, viewport } = await setup();
  document.activeElement = canvas;          // e.g. stroke after stroke while drawing

  viewport.listeners.pointerdown[0]({ target: canvas });
  assert.equal(canvas.focusCalls.length, 0,
    'no redundant focus() while the canvas is already focused');
});

test('the canvas markup is focusable, without joining the Tab order', async () => {
  const { StencilMainContent } = await import('../js/ui/panel/mainContent.js');
  const markup = StencilMainContent.inner();
  // -1 exactly: focusable, never a Tab stop (0 would add the image to Tab navigation).
  assert.match(markup, /<canvas id="canvas" tabindex="-1"><\/canvas>/,
    'the canvas needs tabindex="-1" for .focus() to have any effect');
});
