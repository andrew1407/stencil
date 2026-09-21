// DrawingApp.removeSelectedLines() (js/core/drawingApp.js) — the multi-select counterpart of removeLine(), and
// what a bare Delete / Backspace calls. Only the modified chord (Alt+Delete, ⌥⌫ on Mac) was wired to the canvas
// selection and it removed a SINGLE index, so on a laptop — where the key labelled "delete" is Backspace and
// forward-Delete needs Fn — pressing delete with lines selected did nothing.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// A stand-in with just the collaborators removeSelectedLines touches, so the test needs
// no DOM. The method is taken off DrawingApp's prototype and applied to it.
import { DrawingApp } from '../../js/core/drawingApp.js';

const makeApp = (lineCount, { selectedLines = [], selectedLineIdx = -1, coordLineIdx = -1 } = {}) => {
  const app = {
    lines: Array.from({ length: lineCount }, (_, i) => ({ id: i, points: [{ x: i, y: i }] })),
    selectedLines,
    selectedLineIdx,
    coordLineIdx,
    focusedPtIdx: -1,
    hoveredPtIdx: -1,
    historySaves: 0,
    redraws: 0,
    selectedIndices: DrawingApp.prototype.selectedIndices,
    removeSelectedLines: DrawingApp.prototype.removeSelectedLines,
    deselectLine(redraw = true) {
      this.selectedLineIdx = -1;
      this.selectedLines = [];
      this.coordLineIdx = -1;
      this.focusedPtIdx = -1;
      if (redraw) this.redraws++;
    },
    hideSelectionPanels() { this.panelsHidden = true; },
    saveHistory() { this.historySaves++; },
    renderer: { redraw() { app.redraws++; } },
    updateButtons() {},
    updateMultiSelectStatus() {},
    coordTable: { update(points, idx) { app.coordUpdate = { n: points ? points.length : null, idx }; } },
  };
  return app;
};

test('removes every selected line, not just the first', () => {
  const app = makeApp(5, { selectedLines: [0, 2, 4] });
  app.removeSelectedLines();
  assert.equal(app.lines.length, 2);
  assert.deepEqual(app.lines.map(l => l.id), [1, 3], 'the unselected lines survive, in order');
});

// Splicing low-to-high would shift the later indices and delete the wrong lines — the
// reason removeSelectedLines sorts descending.
test('removes the RIGHT lines regardless of the order the set was built in', () => {
  const app = makeApp(5, { selectedLines: [4, 0, 2] });   // click order, not sorted
  app.removeSelectedLines();
  assert.deepEqual(app.lines.map(l => l.id), [1, 3]);
});

test('falls back to the single selection when not multi-selecting', () => {
  const app = makeApp(3, { selectedLineIdx: 1 });
  app.removeSelectedLines();
  assert.deepEqual(app.lines.map(l => l.id), [0, 2]);
});

test('is one history entry, so a single undo restores the whole batch', () => {
  const app = makeApp(4, { selectedLines: [0, 1, 2] });
  app.removeSelectedLines();
  assert.equal(app.historySaves, 1, 'one saveHistory for three removed lines');
});

test('clears the selection afterwards', () => {
  const app = makeApp(4, { selectedLines: [1, 2] });
  app.removeSelectedLines();
  assert.deepEqual(app.selectedLines, []);
  assert.equal(app.selectedLineIdx, -1);
  assert.equal(app.selectedIndices().length, 0);
});

// The coord table can target a line that is NOT selected; its index has to follow the
// splices, exactly as removeLine() shifts it.
test('shifts the coord-table target down past every removed line below it', () => {
  const app = makeApp(6, { selectedLines: [0, 1], coordLineIdx: 4 });
  app.removeSelectedLines();
  assert.equal(app.coordLineIdx, 2, 'two lines below it were removed');
});

test('drops the coord-table target when that line was itself removed', () => {
  const app = makeApp(4, { selectedLines: [2], coordLineIdx: 2 });
  app.removeSelectedLines();
  assert.equal(app.coordLineIdx, -1);
  assert.equal(app.focusedPtIdx, -1);
});

test('is a no-op with nothing selected', () => {
  const app = makeApp(3);
  app.removeSelectedLines();
  assert.equal(app.lines.length, 3);
  assert.equal(app.historySaves, 0, 'no history entry for a no-op');
});

test('ignores stale indices past the end of the line list', () => {
  const app = makeApp(2, { selectedLines: [0, 7] });
  app.removeSelectedLines();
  assert.deepEqual(app.lines.map(l => l.id), [1]);
});
