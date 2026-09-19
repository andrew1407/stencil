// The canvas hit-test and hover-cache paths (js/core/drawingApp.js). Pinned: removePoint/removeLine must not
// leave `hoverPt` and the line-hover fields on pre-removal indices, or the renderer rings a different point
// until the next mousemove; and the hit thresholds are screen-constant (base / scale), since fixed IMAGE px
// shrink the grab radius to nothing zoomed out and grab from half a screen away zoomed in.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DrawingApp } from '../js/core/drawingApp.js';

const makeApp = (lines, { scale = 1 } = {}) => {
  const app = {
    lines,
    currentLine: null,
    scale,
    hoverPt: null,
    hoverLineIdx: -1,
    listHoverLineIdx: -1,
    hoveredPtIdx: -1,
    focusedPtIdx: -1,
    selectedLineIdx: -1,
    coordLineIdx: -1,
    findLineAt: DrawingApp.prototype.findLineAt,
    findNearestPointWithIdx: DrawingApp.prototype.findNearestPointWithIdx,
    findNearestSegmentWithIdx: DrawingApp.prototype.findNearestSegmentWithIdx,
    removePoint: DrawingApp.prototype.removePoint,
    removeLine: DrawingApp.prototype.removeLine,
    canvasCoords: DrawingApp.prototype.canvasCoords,
    deselectLine() { this.selectedLineIdx = -1; },
    saveHistory() {},
    updateButtons() {},
    renderer: { redraw() {} },
    coordTable: { update() {} },
  };
  return app;
};

test('removePoint invalidates the cached canvas hover', () => {
  const app = makeApp([{ points: [{ x: 0, y: 0 }, { x: 50, y: 0 }, { x: 100, y: 0 }] }]);
  app.hoverPt = { lineIdx: 0, ptIdx: 2 };   // hovering the LAST point
  app.hoverLineIdx = 0;
  app.listHoverLineIdx = 0;
  app.removePoint(0, 0);                    // removing the FIRST shifts the indices
  assert.equal(app.hoverPt, null, 'stale hoverPt would ring a different point');
  assert.equal(app.hoverLineIdx, -1);
  assert.equal(app.listHoverLineIdx, -1);
});

test('removeLine invalidates the cached canvas hover', () => {
  const app = makeApp([
    { points: [{ x: 0, y: 0 }, { x: 5, y: 5 }] },
    { points: [{ x: 50, y: 50 }, { x: 60, y: 60 }] },
  ]);
  app.hoverPt = { lineIdx: 1, ptIdx: 0 };
  app.hoverLineIdx = 1;
  app.removeLine(0);                        // line 1 becomes line 0
  assert.equal(app.hoverPt, null);
  assert.equal(app.hoverLineIdx, -1);
});

test('hit thresholds are screen-constant: they scale with the zoom', () => {
  const lines = [{ points: [{ x: 0, y: 0 }, { x: 100, y: 0 }] }];

  // Zoomed OUT (scale 0.25): 12 screen px = 48 image px — a 30 image-px miss at the old
  // fixed threshold is a comfortable hover now.
  const far = makeApp(lines, { scale: 0.25 });
  assert.ok(far.findNearestPointWithIdx(0, 30), 'hover works while zoomed out');
  assert.ok(far.findNearestSegmentWithIdx(50, 30), 'segment hit works while zoomed out');
  assert.notEqual(far.findLineAt(50, 20), -1, 'line hit works while zoomed out');

  // Zoomed IN (scale 4): 12 screen px = 3 image px — a 5 image-px miss must NOT grab.
  const near = makeApp(lines, { scale: 4 });
  assert.equal(near.findNearestPointWithIdx(0, 5), null, 'no far-away grabs while zoomed in');
  assert.equal(near.findNearestSegmentWithIdx(50, 5), null);
  assert.equal(near.findLineAt(50, 13), -1);

  // An explicit threshold still wins (callers that pass one are untouched).
  assert.ok(near.findNearestPointWithIdx(0, 5, 6), 'explicit threshold overrides the default');
});

test('canvasCoords maps through the LIVE on-screen size, not the stale scale', () => {
  const app = makeApp([]);
  app.scale = 1;   // stale: the canvas is actually rendered at 2x (mid-transition / dpr)
  app.canvas = {
    width: 100,
    height: 50,
    getBoundingClientRect: () => ({ left: 10, top: 20, width: 200, height: 100 }),
  };
  const { cssX, cssY, x, y } = app.canvasCoords(110, 70);
  assert.equal(cssX, 100);
  assert.equal(cssY, 50);
  assert.equal(x, 50, 'image x derives from rect.width / canvas.width');
  assert.equal(y, 25, 'image y derives from rect.height / canvas.height');
});

test('canvasCoords falls back to this.scale when the rect has no size', () => {
  const app = makeApp([]);
  app.scale = 2;
  app.canvas = {
    width: 100,
    height: 50,
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 0, height: 0 }),
  };
  const { x, y } = app.canvasCoords(30, 10);
  assert.equal(x, 15);
  assert.equal(y, 5);
});
