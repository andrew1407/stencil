// A hidden kind of mark is out of reach of every pointer path, each driven through the app's real
// hit-test defaults (core/app/editing.js → pointer/markHits.js): click-select, Ctrl+click, the close
// click, hover, the tooltip, double-click delete, Alt+wheel, a touch grab and the Alt+Ctrl pull-out.
// Drawing a new point keeps working. Desktop twin: tests/canvas/input/hiddenMarks.headless.cpp.
import test from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../helpers/dom.js';

installDom();
const { EditingMethods } = await import('../../js/core/app/editing.js');
const { canvasClick } = await import('../../js/core/pointer/canvasClick.js');
const { canvasMouseMove, canvasDblClick } = await import('../../js/core/pointer/hoverController.js');
const { decideHover } = await import('../../js/ui/tip/tooltipHover.js');
const { adjustThicknessAtCursor } = await import('../../js/core/line/editOps.js');
const { grabTouchTarget } = await import('../../js/core/touch/drag.js');

const pts = (...xy) => xy.map(([x, y]) => ({ x, y }));
// One open corner line; client px are image px (the canvas sits at the origin at 1:1).
const rig = ({ showPoints = true, showLines = true, ...over } = {}) => {
  const app = {
    lines: [{ points: pts([100, 100], [300, 100], [300, 300]), thickness: 3 }], currentLine: null,
    scale: 1, showPoints, showLines, image: {}, isDrawing: false, drawMode: 'line',
    selectedLineIdx: -1, selectedLines: [], coordLineIdx: -1, focusedPtIdx: -1, hoveredPtIdx: -1,
    hoverPt: null, hoverLineIdx: -1, continueLineIdx: -1, continueInsertIdx: -1, undonePoints: [],
    color: '#f00', thickness: 2, pointSize: 4, style: 'solid',
    strokeFx: { flyIn() {}, release() {}, cancel() {} },
    coordTable: { update() {}, applyRowHighlight() {} }, coordinatesBody: { querySelector: () => null },
    renderer: { redraw() {}, requestRedraw() {}, effectiveCompareMode: () => 'none' },
    tooltip: { hide() {}, applyHover() {} }, input: { holdEngaged: false },
    canvas: { width: 1000, height: 1000, style: {}, getBoundingClientRect: () => ({ left: 0, top: 0, width: 1000, height: 1000 }) },
    showSelectionPanel() {}, hideSelectionPanels() {}, saveHistory() {}, compareReadOnly: () => false,
    compareShowsPoint: () => true, applyLinesListHover() {}, updateCoordStatus() {},
    deselectLine() { this.selectedLineIdx = -1; this.focusedPtIdx = -1; },
    ...over,
  };
  for (const m of ['findLineAt', 'findNearestPoint', 'findNearestPointWithIdx', 'findNearestSegmentWithIdx', 'beginPullOutDrag'])
    app[m] = EditingMethods.prototype[m];
  return app;
};
const click = (app, x, y, mods = {}) => canvasClick(app, { clientX: x, clientY: y, detail: 1, ...mods });
const VERTEX = [300, 100];
const BODY = [200, 103];

test('a click on a vertex: the point while points show, else its stroke, else nothing', () => {
  const shown = rig();
  click(shown, ...VERTEX);
  assert.deepEqual([shown.selectedLineIdx, shown.focusedPtIdx], [0, 1]);
  const noPoints = rig({ showPoints: false });
  click(noPoints, ...VERTEX);
  assert.deepEqual([noPoints.selectedLineIdx, noPoints.focusedPtIdx], [0, -1], 'the line, no focused point');
  const none = rig({ showPoints: false, showLines: false, selectedLineIdx: 0 });
  click(none, ...VERTEX);
  assert.equal(none.selectedLineIdx, -1, 'a click where nothing shows deselects');
});

test('a click on a hidden stroke selects nothing; its shown points still do', () => {
  const dots = rig({ showLines: false, selectedLineIdx: 0 });
  click(dots, ...BODY);
  assert.equal(dots.selectedLineIdx, -1);
  click(dots, 299, 101);
  assert.deepEqual([dots.selectedLineIdx, dots.focusedPtIdx], [0, 1]);
});

test('Ctrl+click never inserts into a hidden stroke: it adds a point of its own', () => {
  const dots = rig({ showLines: false });
  click(dots, ...BODY, { ctrlKey: true });
  assert.equal(dots.lines[0].points.length, 3, 'the hidden line is untouched');
  assert.deepEqual(dots.lines.at(-1).points, pts(BODY), 'a new one-point line instead');
  const shown = rig();
  click(shown, ...BODY, { ctrlKey: true });
  assert.equal(shown.lines[0].points.length, 4, 'a shown stroke takes the point');
});

test('drawing: with points hidden the first point closes nothing — the click is a new point', () => {
  const stroke = () => ({ points: pts([500, 500], [600, 500], [600, 600]) });
  const shown = rig({ isDrawing: true, currentLine: stroke() });
  click(shown, 501, 501);
  assert.equal(shown.lines.length, 2, 'shown, the first point closes the shape');
  const hidden = rig({ showPoints: false, isDrawing: true, currentLine: stroke() });
  click(hidden, 501, 501);
  assert.equal(hidden.lines.length, 1, 'nothing closed');
  assert.equal(hidden.currentLine.points.length, 4, 'and the click drew its point');
  assert.equal(hidden.isDrawing, true);
});

test('hover: no ring, row tint or pointer cursor from a hidden mark', () => {
  const move = (app, [x, y]) => canvasMouseMove(app, { clientX: x, clientY: y, altKey: false, shiftKey: false, ctrlKey: false, metaKey: false });
  const shown = rig();
  move(shown, VERTEX);
  assert.deepEqual([shown.hoverPt, shown.hoverLineIdx, shown.canvas.style.cursor], [{ lineIdx: 0, ptIdx: 1 }, 0, 'pointer']);
  const noPoints = rig({ showPoints: false });
  move(noPoints, VERTEX);
  assert.deepEqual([noPoints.hoverPt, noPoints.hoverLineIdx], [null, 0], 'no ring; the shown stroke is still under it');
  const dots = rig({ showLines: false });
  move(dots, BODY);
  assert.deepEqual([dots.hoverLineIdx, dots.canvas.style.cursor], [-1, 'crosshair']);
  const none = rig({ showPoints: false, showLines: false });
  move(none, VERTEX);
  assert.deepEqual([none.hoverPt, none.hoverLineIdx, none.canvas.style.cursor], [null, -1, 'crosshair']);
});

test('the tooltip labels no hidden point, and no line while nothing of it shows', () => {
  const NO_MODS = { altKey: false, ctrlKey: false, metaKey: false, shiftKey: false };
  const keysAt = (app, [x, y]) => {
    const keys = [];
    decideHover({ app, scheduleShow: (key) => keys.push(key), hide() {} }, x, y, x, y, NO_MODS);
    return keys;
  };
  assert.deepEqual(keysAt(rig(), VERTEX), ['point:300:100']);
  assert.deepEqual(keysAt(rig({ showPoints: false }), VERTEX), ['line:0:false'], 'the stroke answers instead');
  assert.deepEqual(keysAt(rig({ showLines: false }), BODY), []);
  assert.deepEqual(keysAt(rig({ showPoints: false, showLines: false }), VERTEX), []);
});

test('double-click, Alt+wheel, a touch grab and the pull-out reach no hidden mark', () => {
  const dbl = (app, [x, y]) => canvasDblClick(app, { clientX: x, clientY: y, altKey: false });
  const dots = rig({ showLines: false });
  dbl(dots, BODY);
  assert.equal(dots.lines.length, 1, 'a double-click on a hidden stroke deletes nothing');
  const shown = rig();
  dbl(shown, BODY);
  assert.equal(shown.lines.length, 0, 'on a shown one it deletes the line');

  const wheel = (app, [x, y]) => adjustThicknessAtCursor(app, { clientX: x, clientY: y, deltaY: -1 }, () => {});
  assert.equal(wheel(rig({ showPoints: false, showLines: false }), VERTEX), false);
  assert.equal(wheel(dots, BODY), false);
  assert.equal(wheel(rig({ showPoints: false }), BODY), true, 'a shown stroke still takes the wheel');

  const touch = (app, [x, y]) => grabTouchTarget(app, { clientX: x, clientY: y });
  const none = rig({ showPoints: false, showLines: false });
  assert.equal(touch(none, VERTEX), null);
  assert.equal(touch(rig({ showPoints: false }), VERTEX), 'segment', 'a hidden vertex leaves its stroke to grab');
  assert.equal(touch(rig(), VERTEX), 'point');

  assert.equal(none.beginPullOutDrag(...VERTEX), false);
  assert.equal(none.lines[0].points.length, 3);
  assert.equal(rig().beginPullOutDrag(...BODY), true, 'a shown stroke still gives a point up');
});
