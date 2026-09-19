// The independent point colour (Line.pointColor), a port of the pointColor cases in
// core/tests/rasterize.test.cpp: the browser strokes with canvas 2D, so `pointColorOf` is the JS twin of
// core's `pointColorOr` and the two must agree — including that EMPTY means "inherit the stroke colour".

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { pointColorOf } from '../js/core/renderer.js';
import { sanitizeLines, mergeLines } from '../js/core/layout.js';

test('pointColorOf falls back to the stroke colour when unset', () => {
  assert.equal(pointColorOf({ color: '#FFFF00' }), '#FFFF00');
  assert.equal(pointColorOf({ color: '#FFFF00', pointColor: '' }), '#FFFF00',
    'empty string is "unset", not a colour — that is how it serialises');
  assert.equal(pointColorOf({ color: '#FFFF00', pointColor: undefined }), '#FFFF00');
});

test('pointColorOf uses the line\'s own point colour when set', () => {
  assert.equal(pointColorOf({ color: '#FFFF00', pointColor: '#0000FF' }), '#0000FF');
});

// An untrusted layout (#stencil= fragment, pasted JSON, co-edit payload) is rebuilt onto a
// fresh object from a field whitelist; a new field has to be added there or it is dropped.
test('sanitizeLines keeps a string pointColor and drops a non-string one', () => {
  const [kept] = sanitizeLines([{ points: [{ x: 1, y: 2 }], color: 'red', pointColor: '#00FF00' }]);
  assert.equal(kept.pointColor, '#00FF00');

  const [coerced] = sanitizeLines([{ points: [{ x: 1, y: 2 }], color: 'red', pointColor: 42 }]);
  assert.ok(!('pointColor' in coerced), 'a non-string is dropped, leaving the fallback');
});

test('sanitizeLines still refuses prototype-polluting keys alongside it', () => {
  const [line] = sanitizeLines([
    { points: [{ x: 0, y: 0 }], pointColor: '#123456', __proto__: { evil: 1 }, constructor: 'x' },
  ]);
  assert.equal(line.pointColor, '#123456');
  assert.ok(!Object.prototype.hasOwnProperty.call(line, 'constructor'));
  assert.equal(line.evil, undefined);
});

// mergeLines is the co-edit union merge, keyed on a per-line dedupe key: two lines differing only in point
// colour are genuinely different lines, so the key includes pointColor or one editor's recolour vanishes.
const BASE = { points: [{ x: 0, y: 0 }, { x: 5, y: 5 }], color: 'red', thickness: 2,
               pointSize: 4, style: 'solid', locked: false, fillColor: 'transparent' };

test('mergeLines keeps two lines that differ only by pointColor', () => {
  const merged = mergeLines([{ ...BASE, pointColor: '#0000FF' }], [{ ...BASE, pointColor: '#00FF00' }]);
  assert.equal(merged.length, 2);
});

test('mergeLines still collapses a local line against its server twin', () => {
  const line = { ...BASE, pointColor: '#0000FF' };
  assert.equal(mergeLines([line], [{ ...line }]).length, 1);
});

// An unset point colour must key the SAME whether it round-tripped as absent or as ''
// — otherwise a server copy and its local original stop deduping.
test('mergeLines treats an absent pointColor and an empty one as the same line', () => {
  const merged = mergeLines([{ ...BASE }], [{ ...BASE, pointColor: '' }]);
  assert.equal(merged.length, 1);
});

// Points take the point-colour SETTING at draw time (empty setting → the line colour then), and a later
// line-colour change never recolours drawn points: a new line materialises its pointColor at creation.
import { DrawingApp } from '../js/core/drawingApp.js';

const makeDrawApp = () => {
  const app = {
    image: {},                       // startDrawingMode requires an image
    lines: [],
    currentLine: null,
    isDrawing: false,
    color: '#ff0000',
    pointColor: '',                  // "follow the line colour" setting
    thickness: 2,
    pointSize: 4,
    style: 'solid',
    selectedLineIdx: -1,
    selectedLines: [],
    coordLineIdx: -1,
    focusedPtIdx: -1,
    continueLineIdx: -1,
    continueInsertIdx: -1,
    undonePoints: [],
    startDrawingMode: DrawingApp.prototype.startDrawingMode,
    applySelectionChange: DrawingApp.prototype.applySelectionChange,
    compareReadOnly() { return false; },
    hideSelectionPanels() {},
    saveHistory() {},
    updateButtons() {},
    renderer: { redraw() {} },
    coordTable: { update() {} },
  };
  return app;
};

test('a new line resolves its point colour at draw time', () => {
  const app = makeDrawApp();
  app.startDrawingMode();
  assert.equal(app.currentLine.pointColor, '#ff0000',
    'empty point-colour setting resolves to the line colour of that moment');

  const explicit = makeDrawApp();
  explicit.pointColor = '#00ff00';
  explicit.startDrawingMode();
  assert.equal(explicit.currentLine.pointColor, '#00ff00');
});

test('recolouring a line leaves its rendered point colour untouched', () => {
  const app = makeDrawApp();
  // An old line still on the inherit fallback ('' — pre-pointColor layouts).
  app.lines = [{ points: [{ x: 0, y: 0 }, { x: 5, y: 5 }], color: '#ff0000', pointColor: '' }];
  app.selectedLineIdx = 0;
  app.applySelectionChange('color', '#0000ff');
  assert.equal(app.lines[0].color, '#0000ff');
  assert.equal(pointColorOf(app.lines[0]), '#ff0000',
    'points keep drawing in the colour they had before the stroke recolour');
});

test('an explicit point colour survives a line recolour unchanged', () => {
  const app = makeDrawApp();
  app.lines = [{ points: [{ x: 0, y: 0 }], color: '#ff0000', pointColor: '#123456' }];
  app.selectedLineIdx = 0;
  app.applySelectionChange('color', '#0000ff');
  assert.equal(app.lines[0].pointColor, '#123456');
});
