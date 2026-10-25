// The stored form of a history step (js/core/historyStack.js): plain {x, y} points are kept packed in
// one Float64Array, every other field deep-copied, and undo/redo hand back plain, mutable lines that
// equal what was pushed — the packing is invisible from outside.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { HistoryStack } from '../../js/core/historyStack.js';

const memento = (lines) => ({ lines, cropRect: { x: 1, y: 2, width: 3, height: 4 }, rotationQuarters: 1, mirrored: false, filter: 'none', filterColor: '' });

test('a stored step packs plain points, and undo/redo return them unpacked and equal', () => {
  const h = new HistoryStack();
  const a = memento([{ color: '#f00', name: 'L', points: [{ x: 1.5, y: 2.25 }, { x: -3, y: 4e9 }] }]);
  const b = memento([{ color: '#0f0', points: [{ x: 7, y: 8 }] }]);
  h.reset(a, 0);
  h.push(b);
  assert.ok(h.history[0].lines[0].points instanceof Float64Array, 'packed at rest');
  assert.deepEqual(h.undo(), a);
  assert.deepEqual(h.redo(), b);
});

test('what undo hands back is free to mutate, and live edits never reach a stored step', () => {
  const h = new HistoryStack();
  const live = memento([{ points: [{ x: 1, y: 1 }] }]);
  h.reset(live, 0);
  h.push(memento([{ points: [{ x: 2, y: 2 }] }]));
  live.lines[0].points[0].x = 999;
  const back = h.undo();
  assert.equal(back.lines[0].points[0].x, 1);
  back.lines[0].points[0].x = 555;
  back.cropRect.x = 555;
  assert.equal(h.history[0].cropRect.x, 1, 'the stored view is untouched');
  h.redo();
  assert.deepEqual(h.undo().lines[0].points[0], { x: 1, y: 1 });
});

test('a point that carries more than x and y keeps everything it carries', () => {
  const h = new HistoryStack();
  const odd = [{ points: [{ x: 1, y: 2, t: 99 }, { x: 3, y: 4 }] }];
  h.reset(odd, 0);
  h.push([]);
  assert.deepEqual(h.undo(), odd);
});

test('a bare Lines snapshot and a line without points round-trip too', () => {
  const h = new HistoryStack();
  const lines = [{ id: 'a', points: [{ x: 0, y: 0 }] }, { id: 'b' }];
  h.reset(lines, 0);
  h.push([]);
  assert.deepEqual(h.undo(), lines);
});

test('a step keeps every key in its place, so the JSON of what comes back is the JSON pushed', () => {
  const h = new HistoryStack();
  const a = { lines: [{ name: 'n', points: [{ x: 1, y: 2 }], color: '#00ff00', hidden: false }], cropRect: { x: 0 }, filter: 'none' };
  h.reset(a, 0);
  h.push({ ...a, lines: [] });
  assert.equal(JSON.stringify(h.undo()), JSON.stringify(a));
});
