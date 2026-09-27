// Ctrl/⌘+click away from every segment, while not drawing, adds a point: a new one-point line,
// or — with a line selected — a point connected to it (shapeBuilder.addConnectedPoint).
import test from 'node:test';
import assert from 'node:assert';

import { canvasClick } from '../../../js/core/pointer/canvasClick.js';

const stubApp = (over = {}) => ({
  lines: [], currentLine: null, selectedLineIdx: -1, coordLineIdx: -1, focusedPtIdx: -1,
  isDrawing: false, drawMode: 'line', image: {}, color: '#f00', thickness: 2, pointSize: 4,
  style: 'solid', strokeFx: { flyIn() {} }, coordTable: { update() {} }, renderer: { redraw() {} },
  showSelectionPanel() {}, saveHistory() {},
  compareReadOnly: () => false,
  // Client px are image px: the canvas sits at the origin at 1:1 (pointer/canvasCoords.js).
  canvas: { width: 100, height: 100, style: {}, getBoundingClientRect: () => ({ left: 0, top: 0, width: 100, height: 100 }) },
  findNearestSegmentWithIdx: () => null,
  ...over,
});

for (const [name, mod] of [['Ctrl', { ctrlKey: true }], ['⌘', { metaKey: true }]]) {
  test(`${name}+click on empty canvas starts a one-point line and selects it`, () => {
    const app = stubApp();
    assert.doesNotThrow(() => canvasClick(app, { clientX: 30, clientY: 40, ...mod }));
    assert.equal(app.lines.length, 1);
    assert.deepEqual(app.lines[0].points, [{ x: 30, y: 40 }]);
    assert.equal(app.selectedLineIdx, 0);
  });
}

test('Ctrl+click with a line selected connects the point to that line', () => {
  const app = stubApp({ lines: [{ points: [{ x: 0, y: 0 }, { x: 10, y: 0 }] }], selectedLineIdx: 0 });
  canvasClick(app, { clientX: 20, clientY: 5, ctrlKey: true });
  assert.equal(app.lines.length, 1);
  assert.deepEqual(app.lines[0].points.at(-1), { x: 20, y: 5 });
});
