// The line panels follow the edits that change a line in place (desktop twin:
// MainWindow.lineRows.gui.cpp): an unchain, a chain break, a thickness wheeled on the canvas and a
// fill picked in the bar each leave the Lines tab, the points table and the bar true.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { line, makeApp, listRows, assertPanels } from '../helpers/linePanelsRig.js';

const { selectLineFromList, applySelectionChange } = await import('../../js/core/line/selection.js');
const { startDrawingMode } = await import('../../js/core/draw/mode.js');
const { breakChainAt } = await import('../../js/core/draw/chainBreak.js');
const { adjustThicknessAtCursor } = await import('../../js/core/line/editOps.js');

test('an unchain and a chain break leave the panels true', () => {
  const app = makeApp([line(20), { ...line(60, 4), locked: true, fillColor: '#00ff0080' }]);
  selectLineFromList(app, 1);
  app.unchainSelectedLine();
  assertPanels(app, 'after the unchain');
  app.deselectLine();
  startDrawingMode(app, { connect: false });
  app.currentLine.points.push({ x: 200, y: 200 }, { x: 260, y: 220 });
  breakChainAt(app, 300, 250);
  assertPanels(app, 'after the chain break');
  assert.equal(listRows().length, 3, 'the kept stroke is a row');
});

test('the Lines tab follows a thickness wheeled on the canvas and a fill picked in the bar', () => {
  const app = makeApp([line(20), { ...line(60), locked: true }]);
  adjustThicknessAtCursor(app, { clientX: 60, clientY: 20, deltaY: -1 }, () => {});
  assert.equal(listRows()[0].children[3].textContent, '3', 'the wheel\'s new thickness');
  selectLineFromList(app, 1);
  applySelectionChange(app, 'fillColor', '#336699');
  assert.equal(listRows()[1].children[2].children[0].style.background, '#336699', 'the area swatch shows its fill');
});

// A turn or a flip moves the same lines, so what was picked stays picked; an undo or redo of it too.
const { ImageModel } = await import('../../js/core/image/model.js');
const { toggleLineSelection, selectedIndices } = await import('../../js/core/line/selection.js');
const withModel = (lines) => {
  const app = makeApp(lines);
  app.cropRect = { x: 0, y: 0, width: 400, height: 300 };
  app.zoomPan = { ...app.zoomPan, fitToWindow() {} };
  app.imageModel = new ImageModel(app);
  app.imageModel.rebuildCroppedImage = () => {};
  return app;
};

test('rotate left and right and a flip keep a selected line, its bar and both lists', () => {
  const app = withModel([line(20), line(60, 4), line(100)]);
  selectLineFromList(app, 1);
  const picked = app.lines[1];
  for (const turn of [() => app.imageModel.rotateImage(-1), () => app.imageModel.rotateImage(1), () => app.imageModel.flipImage()]) {
    turn();
    assert.equal(app.lines[app.selectedLineIdx], picked, 'the same line stays selected');
    assertPanels(app, 'after a turn or flip');
  }
  app.undo();
  assert.equal(app.selectedLineIdx, 1, 'an undo of a turn keeps the line selected');
  assertPanels(app, 'after the undo');
  app.redo();
  assert.equal(app.selectedLineIdx, 1);
  assertPanels(app, 'after the redo');
});

test('a turn and a flip keep a multi-selection', () => {
  const app = withModel([line(20), line(60), line(100)]);
  toggleLineSelection(app, 0);
  toggleLineSelection(app, 2);
  app.imageModel.rotateImage(1);
  app.imageModel.flipImage();
  assert.deepEqual(selectedIndices(app), [0, 2]);
  assertPanels(app, 'after a turn and a flip');
  app.undo();
  assert.deepEqual(selectedIndices(app), [0, 2]);
  assertPanels(app, 'after the undo');
});
