// Alt+P / Alt+L with the pointer resting on the canvas hover again under the keys held, so a mark
// just hidden keeps no ring, Lines-row tint or pointer cursor, and one just shown is hovered at once
// (ui/bindings/keys/hotkeyActions.js). Desktop twin: CanvasWidget::refreshHoverForModifiers.
import test from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../../helpers/dom.js';

const doc = installDom({}, { window: { addEventListener() {} } });
const { hotkeyActions } = await import('../../../../js/ui/bindings/keys/hotkeyActions.js');
const { EditingMethods } = await import('../../../../js/core/app/editing.js');

// The view checkboxes, without a box to dust.
for (const id of ['show-points', 'show-lines'])
  doc.register(id, createStubElement('input', { type: 'checkbox', checked: true, getBoundingClientRect: undefined }));

// One line, the pointer resting at (x, y) and hovering what is there; client px are image px.
const rig = (x, y, over = {}) => {
  const app = {
    lines: [{ points: [{ x: 100, y: 100 }, { x: 300, y: 100 }] }], currentLine: null, scale: 1,
    showPoints: true, showLines: true, image: {}, isDrawing: false, drawMode: 'line',
    mouseOverCanvas: true, lastMouseClientX: x, lastMouseClientY: y,
    hoverPt: null, hoverLineIdx: 0, hoveredPtIdx: -1, coordLineIdx: -1,
    renderer: { redraw() {} }, tooltip: { hide() {}, applyHover() {} }, input: { holdEngaged: false },
    canvas: { width: 1000, height: 1000, style: { cursor: 'pointer' },
      getBoundingClientRect: () => ({ left: 0, top: 0, width: 1000, height: 1000 }) },
    coordTable: { applyRowHighlight() {} }, applyLinesListHover() {}, updateCoordStatus() {},
    compareReadOnly: () => false,
    ...over,
  };
  for (const m of ['findLineAt', 'findNearestPoint', 'findNearestPointWithIdx', 'findNearestSegmentWithIdx'])
    app[m] = EditingMethods.prototype[m];
  return app;
};
const ALT = { altKey: true, shiftKey: false, ctrlKey: false, metaKey: false, timeStamp: 1 };

test('Alt+L over a stroke: the hidden line leaves no tint and no line cursor behind', () => {
  const app = rig(200, 102);
  hotkeyActions(app).HK_HANDLERS.toggleLines(ALT);
  assert.equal(app.showLines, false);
  assert.equal(app.hoverLineIdx, -1, 'the Lines-row tint goes with the stroke');
  assert.equal(app.canvas.style.cursor, 'grab', 'Alt still held: the drag-ready cursor, over nothing');
});

test('Alt+P over a point drops its ring, and Alt+P again brings it back', () => {
  const app = rig(300, 101, { hoverPt: { lineIdx: 0, ptIdx: 1 } });
  const { HK_HANDLERS } = hotkeyActions(app);
  HK_HANDLERS.togglePoints(ALT);
  assert.equal(app.showPoints, false);
  assert.equal(app.hoverPt, null);
  assert.equal(app.hoverLineIdx, 0, 'the shown stroke is still under the pointer');
  HK_HANDLERS.togglePoints(ALT);
  assert.deepEqual(app.hoverPt, { lineIdx: 0, ptIdx: 1 });
});

test('the keydown loop hands its event to the toggle: macOS Option+L, typing "¬", still hovers again', async () => {
  const { wireKeyboard } = await import('../../../../js/ui/bindings/keys/keyboard.js');
  const app = rig(200, 102);
  doc.getElementById('show-lines').checked = true;
  wireKeyboard(app);
  doc.dispatch('keydown', { key: '¬', code: 'KeyL', altKey: true, shiftKey: false, ctrlKey: false, metaKey: false,
    timeStamp: 1, preventDefault() {} });
  assert.equal(app.showLines, false);
  assert.deepEqual([app.hoverLineIdx, app.canvas.style.cursor], [-1, 'grab']);
});

test('with the pointer off the canvas a toggle leaves the hover alone', () => {
  const app = rig(200, 102, { mouseOverCanvas: false });
  hotkeyActions(app).HK_HANDLERS.toggleLines(ALT);
  assert.equal(app.hoverLineIdx, 0);
  assert.equal(app.canvas.style.cursor, 'pointer');
});
