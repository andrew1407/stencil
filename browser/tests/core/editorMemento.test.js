// The history over editor mementos (js/core/historyStack.js): a crop, a turn or a filter switch is
// one undo step, and the step -1 stop keeps the view and filter it started from. Core twin:
// tests/state/editorHistory.test.cpp.
import test from 'node:test';
import assert from 'node:assert/strict';

import { HistoryStack, MAX_STEPS, editorMemento, cursorStep, sameFilter } from '../../js/core/historyStack.js';

const at = (x, rotationQuarters, lineCount, mirrored = false) => ({
  lines: Array.from({ length: lineCount }, (_, i) => ({ points: [{ x: i, y: 1 }] })),
  cropRect: { x, y: 0, width: 50, height: 40 },
  rotationQuarters,
  mirrored,
});

test('a crop and a turn step back and forth with the lines', () => {
  const h = new HistoryStack();
  h.reset(at(0, 0, 0));
  assert.equal(h.canUndo(), false);
  h.push(at(0, 0, 1));
  h.push(at(7, 1, 1));
  assert.deepEqual(h.undo(), at(0, 0, 1));
  assert.deepEqual(h.undo(), at(0, 0, 0), 'the floor: no lines, on the view the stack started on');
  assert.equal(h.undo(), null);
  assert.deepEqual(h.redo(), at(0, 0, 1));
  assert.deepEqual(h.redo(), at(7, 1, 1));
});

test('a crop on a fresh image is undoable', () => {
  const h = new HistoryStack();
  h.reset(at(0, 0, 0));
  h.push(at(9, 0, 0));
  assert.equal(h.canUndo(), true);
  assert.deepEqual(h.undo(), at(0, 0, 0));
  assert.equal(h.historyStep, -1);
});

test('the floor clears the lines but keeps the view, even from a base with lines', () => {
  const h = new HistoryStack();
  h.reset(at(3, 2, 2), 0);
  h.push(at(8, 2, 2));
  h.undo();
  assert.deepEqual(h.undo(), at(3, 2, 0));
});

test('past MAX_STEPS the floor takes the view of the last step dropped', () => {
  const h = new HistoryStack();
  h.reset(at(-1, 0, 0));
  for (let i = 0; i < 70; i++) h.push(at(i, 0, 1));
  assert.equal(h.history.length, MAX_STEPS);
  for (let i = 0; i < 63; i++) h.undo();
  assert.equal(h.historyStep, 0);
  assert.deepEqual(h.undo(), at(5, 0, 0));
});

const tinted = (x, filter, filterColor, lineCount = 1) => ({ ...at(x, 0, lineCount), filter, filterColor });

test('stored steps are copies: a later in-place turn of the live lines or crop changes none', () => {
  const app = { ...at(0, 0, 1), imageFilter: 'bw', filterColor: '#7c3aed' };
  const h = new HistoryStack();
  h.push(editorMemento(app));
  h.push(editorMemento(app));
  app.lines[0].points[0].x = 99;
  app.cropRect.x = 99;
  assert.deepEqual(h.undo(), tinted(0, 'bw', '#7c3aed'));
});

test('editorMemento captures lines, crop, turn, mirror and the filter over them', () => {
  const m = editorMemento({ lines: [], cropRect: null, rotationQuarters: 2, imageFilter: 'custom', filterColor: '#ff0000' });
  assert.deepEqual(m, { lines: [], cropRect: null, rotationQuarters: 2, mirrored: false, filter: 'custom', filterColor: '#ff0000' });
  assert.equal(editorMemento({ lines: [], cropRect: null, rotationQuarters: 0, mirrored: true }).mirrored, true);
});

test('a flip is a step of its own: undo brings the unmirrored view back', () => {
  const h = new HistoryStack();
  h.reset(at(0, 1, 0));
  h.push(at(10, 3, 1, true));
  assert.deepEqual(h.undo(), at(0, 1, 0), 'the floor: unmirrored, quarter one');
  assert.deepEqual(h.redo(), at(10, 3, 1, true));
});

test('a filter switch and a tint are steps of their own, and the floor keeps the filter it started on', () => {
  const h = new HistoryStack();
  h.reset(tinted(0, 'sepia', '#7c3aed', 0));
  h.push(tinted(0, 'sepia', '#7c3aed'));
  h.push(tinted(0, 'custom', '#7c3aed'));
  h.push(tinted(0, 'custom', '#00ff00'));
  assert.deepEqual(h.undo(), tinted(0, 'custom', '#7c3aed'));
  assert.deepEqual(h.undo(), tinted(0, 'sepia', '#7c3aed'));
  assert.deepEqual(h.undo(), tinted(0, 'sepia', '#7c3aed', 0), 'the floor: no lines, the loaded filter');
  assert.deepEqual(h.redo(), tinted(0, 'sepia', '#7c3aed'));
});

test('the cursor step names the filter on screen; a Lines step names none', () => {
  const app = { imageFilter: 'bw', filterColor: '#7c3aed' };
  const h = new HistoryStack();
  h.reset(tinted(0, 'bw', '#7c3aed', 0));
  assert.equal(h.historyStep, -1);
  assert.equal(sameFilter(cursorStep(h), app), true, 'at step -1 the floor is on screen');
  h.push(tinted(0, 'sepia', '#7c3aed'));
  assert.equal(sameFilter(cursorStep(h), app), false);
  assert.equal(sameFilter(cursorStep(h), { ...app, imageFilter: 'sepia' }), true);
  assert.equal(sameFilter(cursorStep(h), { imageFilter: 'sepia', filterColor: '#000000' }), false, 'the tint counts');
  h.push([{ points: [] }]);
  assert.equal(sameFilter(cursorStep(h), app), false);
  assert.equal(sameFilter(undefined, app), false);
});
