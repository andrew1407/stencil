// Every way a line or point leaves the editor keeps both panel lists true (desktop twin:
// MainWindow.selectionLists.gui.cpp): the Lines tab lists each line with its point count and the
// selection, the points table acts on the very line it lists, and the bar shows the selected line.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { linesBody, linesTable, line, makeApp, listRows, assertPanels, deleteFirstTablePoint } from '../helpers/linePanelsRig.js';

const { renderLinesList } = await import('../../js/ui/panel/lines/list.js');
const { removeLine, removePoint, removeSelectedLines } = await import('../../js/core/line/editOps.js');
const { selectLineFromList, toggleLineSelection, selectedIndices } = await import('../../js/core/line/selection.js');
const { installLayout } = await import('../../js/core/layoutInstall.js');
const { canvasDblClick } = await import('../../js/core/pointer/hoverController.js');
const { hotkeyActions } = await import('../../js/ui/bindings/keys/hotkeyActions.js');
const { applyPeerLayout } = await import('../../js/core/remote/peerLayout.js');

test('a layout install re-targets the points table, so its Delete removes from the line it lists', () => {
  const app = makeApp([line(20)]);
  selectLineFromList(app, 0);
  installLayout(app, { imageWidth: 400, imageHeight: 300, lines: [line(40, 2), line(80, 4)] });
  assertPanels(app, 'after the install');
  deleteFirstTablePoint(app);
  assert.deepEqual(app.lines.map((l) => l.points.length), [2, 3], 'the listed line lost its point');
  assertPanels(app, 'after the table delete');
});

test('undo and redo across a removal leave no stale selection, bar or table', () => {
  const app = makeApp([line(20), line(60, 4), line(100)]);
  selectLineFromList(app, 1);
  removeLine(app, 1);
  assertPanels(app, 'after the removal');
  selectLineFromList(app, 1);
  app.undo();
  assertPanels(app, 'after the undo');
  app.redo();
  assertPanels(app, 'after the redo');
  deleteFirstTablePoint(app);
  assertPanels(app, 'after a table delete');
});

test('an undo of a bar edit never leaves the bar on the replaced line', () => {
  const app = makeApp([line(20), line(60)]);
  selectLineFromList(app, 0);
  app.lines[0].thickness = 9;
  app.saveHistory();
  app.undo();
  assertPanels(app, 'after the undo');
});

test('Clear All Lines drops a multi-selection with the lines', async () => {
  const app = makeApp([line(20), line(60), line(100)]);
  toggleLineSelection(app, 0);
  toggleLineSelection(app, 2);
  await app.clearAllLines();
  assertPanels(app, 'after the clear');
  installLayout(app, { imageWidth: 400, imageHeight: 300, lines: [line(20), line(60), line(100)] });
  assertPanels(app, 'after new lines arrive');
});

test('removing a line keeps a multi-selection on the lines it held', () => {
  const app = makeApp([line(20), line(60), line(100), line(140)]);
  for (const i of [0, 2, 3]) toggleLineSelection(app, i);
  const held = [app.lines[0], app.lines[3]];
  removeLine(app, 2);
  assertPanels(app, 'after the removal');
  assert.deepEqual(selectedIndices(app).map((i) => app.lines[i]), held);
  removeLine(app, 0);
  assertPanels(app, 'down to one: the bar takes it');
  assert.equal(app.lines[app.selectedLineIdx], held[1]);
});

test('emptying a line drops it and shifts the selection past it', () => {
  const app = makeApp([line(20, 1), line(60), line(100)]);
  selectLineFromList(app, 2);
  const picked = app.lines[2];
  removePoint(app, 0, 0);
  assertPanels(app, 'after the last point went');
  assert.equal(app.lines[app.selectedLineIdx], picked, 'the same line stays selected');
});

test('a canvas double-click erase keeps the selection and the table on their lines', () => {
  const app = makeApp([line(20), line(60), line(100)]);
  for (const i of [0, 2]) toggleLineSelection(app, i);
  const held = [app.lines[0], app.lines[2]];
  canvasDblClick(app, { clientX: 40, clientY: 60, altKey: false });
  assert.equal(app.lines.length, 2);
  assertPanels(app, 'after the erase');
  assert.deepEqual(selectedIndices(app).map((i) => app.lines[i]), held);
});

test('the bins, the row keys and the hotkeys all leave the panels true', () => {
  const app = makeApp([line(20), line(60, 4), line(100), line(140)]);
  selectLineFromList(app, 1);
  linesBody.dispatch('keydown', { key: 'Delete', target: listRows()[3], preventDefault() {}, stopPropagation() {} });
  assertPanels(app, 'a Lines-row Delete');
  const bin = { closest: (sel) => (sel === '.lines-remove' ? bin : sel === 'tr.lines-row' ? listRows()[0] : null) };
  linesBody.dispatch('click', { target: bin, stopPropagation() {} });
  assertPanels(app, 'a Lines-row bin');
  deleteFirstTablePoint(app);
  assertPanels(app, 'a points-table Delete');
  const { HK_HANDLERS } = hotkeyActions(app);
  app.focusedPtIdx = 0;
  HK_HANDLERS.deletePoint();
  assertPanels(app, 'Alt+Shift+Delete');
  HK_HANDLERS.deleteLine();
  assertPanels(app, 'Alt+Delete on a focused point');
  selectLineFromList(app, 0);
  toggleLineSelection(app, 1);
  removeSelectedLines(app);
  assertPanels(app, 'a multi-selection delete');
});

test('a peer layout that takes lines away re-shows the bar for the line still selected', () => {
  const app = makeApp([line(20), line(60), line(100)]);
  selectLineFromList(app, 0);
  const remote = { adoptServerPageFormat() {}, adoptServerFilter() {}, adoptServerFormulas() {} };
  const peer = [{ ...line(20), thickness: 7 }, line(60)];
  assert.ok(applyPeerLayout(app, { imageWidth: 400, imageHeight: 300, cropRect: app.cropRect, lines: peer }, remote));
  assertPanels(app, 'after the peer edit');
  assert.equal(app.bar.thickness, 7, 'the bar shows the peer\'s values');
});

test('a list rendered while its tab was hidden is rebuilt when the tab shows', () => {
  const app = makeApp([line(20), line(60)]);
  linesTable.style.display = 'none';
  removeLine(app, 0);
  assert.equal(listRows().length, 2, 'nothing renders behind a hidden tab');
  linesTable.style.display = '';
  renderLinesList(app);
  assertPanels(app, 'once the tab shows');
});
