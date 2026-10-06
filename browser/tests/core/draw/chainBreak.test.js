// Breaking the chain while drawing (js/core/draw/chainBreak.js) through canvasClick: ⌘/Ctrl+click
// and a double-click keep the stroke so far and open an unconnected one, drawing stays on.
// Desktop twin: canvas/draw/CanvasChainBreak.cpp.
import test from 'node:test';
import assert from 'node:assert';

import { canvasClick } from '../../../js/core/pointer/canvasClick.js';
import { tapClickAt } from '../../../js/core/touch/drag.js';

const P = (...xy) => xy.map(([x, y]) => ({ x, y }));
const stubApp = (over = {}) => {
  const app = {
    lines: [], currentLine: { points: [] }, isDrawing: true, drawMode: 'line', image: {}, scale: 1,
    selectedLineIdx: -1, coordLineIdx: -1, focusedPtIdx: -1, continueLineIdx: -1, continueInsertIdx: -1,
    color: '#f00', thickness: 2, pointSize: 4, style: 'solid', undonePoints: [], saves: 0,
    strokeFx: { flyIn() {}, cancel() {} }, renderer: { redraw() {} }, coordTable: { update() {} },
    showSelectionPanel() {}, hideSelectionPanels() {}, saveHistory() { app.saves++; },
    compareReadOnly: () => false,
    // Client px are image px: the canvas sits at the origin at 1:1 (pointer/canvasCoords.js).
    canvas: { width: 1000, height: 1000, style: {}, getBoundingClientRect: () => ({ left: 0, top: 0, width: 1000, height: 1000 }) },
    findNearestSegmentWithIdx: () => null,
    ...over,
  };
  return app;
};
const click = (app, x, y, more = {}) => canvasClick(app, { clientX: x, clientY: y, detail: 1, ...more });

for (const [name, mod] of [['Ctrl', { ctrlKey: true }], ['⌘', { metaKey: true }]]) {
  test(`${name}+click while drawing keeps the stroke and starts an unconnected one there`, () => {
    const app = stubApp();
    click(app, 10, 10);
    click(app, 200, 10);
    click(app, 500, 500, mod);
    assert.equal(app.isDrawing, true);
    assert.deepEqual(app.lines.map((l) => l.points), [P([10, 10], [200, 10])]);
    assert.deepEqual(app.currentLine.points, P([500, 500]));
    assert.equal(app.saves, 1);
    click(app, 600, 500);
    assert.deepEqual(app.currentLine.points, P([500, 500], [600, 500]));
  });
}

test('a lone point is kept, not dropped, when the chain breaks', () => {
  const app = stubApp();
  click(app, 10, 10);
  click(app, 300, 300, { metaKey: true });
  assert.deepEqual(app.lines.map((l) => l.points), [P([10, 10])]);
  assert.deepEqual(app.currentLine.points, P([300, 300]));
});

test('⌘+click on a segment still inserts there instead of breaking', () => {
  const line = { points: P([0, 0], [100, 0]) };
  const app = stubApp({ lines: [line], findNearestSegmentWithIdx: () => ({ lineIdx: 0, ptIdx2: 1 }) });
  click(app, 50, 0, { metaKey: true });
  assert.deepEqual(line.points, P([0, 0], [50, 0], [100, 0]));
  assert.equal(app.lines.length, 1);
});

test('a double-click moves the point its first click dropped onto a new chain', () => {
  const app = stubApp();
  click(app, 10, 10);
  click(app, 200, 10);
  click(app, 400, 400);
  click(app, 401, 400, { detail: 2 });
  assert.equal(app.isDrawing, true);
  assert.deepEqual(app.lines.map((l) => l.points), [P([10, 10], [200, 10])]);
  assert.deepEqual(app.currentLine.points, P([400, 400]));
  click(app, 400, 400, { detail: 3 });
  assert.deepEqual(app.currentLine.points, P([400, 400]), 'a third click adds nothing');
});

test('a double-click while continuing a line ends the continuation and keeps the line', () => {
  const line = { points: P([0, 0], [100, 0]) };
  const app = stubApp({ lines: [line], currentLine: null, continueLineIdx: 0, continueInsertIdx: 2 });
  click(app, 100, 100);
  click(app, 100, 100, { detail: 2 });
  assert.deepEqual(line.points, P([0, 0], [100, 0]));
  assert.equal(app.continueLineIdx, -1);
  assert.deepEqual(app.currentLine.points, P([100, 100]));
  assert.equal(app.lines.length, 1);
});

test('a plain click sequence still builds one connected chain', () => {
  const app = stubApp();
  for (const [x, y] of [[0, 0], [50, 0], [50, 50]]) click(app, x, y);
  assert.deepEqual(app.currentLine.points, P([0, 0], [50, 0], [50, 50]));
  assert.equal(app.lines.length, 0);
});

test('a double-tap breaks the chain like a double-click; taps apart do not', () => {
  const app = stubApp();
  const tap = (x, y) => tapClickAt(app, { changedTouches: [{ clientX: x, clientY: y }] }, { startX: x, startY: y });
  tap(10, 10);
  tap(300, 10);
  assert.deepEqual(app.currentLine.points, P([10, 10], [300, 10]), 'two taps far apart connect');
  tap(300, 300);
  tap(302, 301);
  assert.deepEqual(app.lines.map((l) => l.points), [P([10, 10], [300, 10])]);
  assert.deepEqual(app.currentLine.points, P([300, 300]));
});
