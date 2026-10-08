// Only what is drawn is hit (js/core/pointer/markHits.js and the app's hit-test defaults in
// core/app/editing.js): hidden points are never a point target, hidden lines never a segment
// target, and with both hidden nothing is. Desktop twin: tests/model/markHits.headless.cpp.
import test from 'node:test';
import assert from 'node:assert/strict';

import { shownMarks, lineAt, holdTargetAt } from '../../../js/core/pointer/markHits.js';
import { findLineAt } from '../../../js/core/draw/hitTest.js';
import { EditingMethods } from '../../../js/core/app/editing.js';

const pts = (...xy) => xy.map(([x, y]) => ({ x, y }));
// An open corner line and a lone point (a lone point is drawn only as a point).
const LINES = [{ points: pts([0, 0], [100, 0], [100, 100]) }, { points: pts([300, 300]) }];
const BOTH = { points: true, lines: true };
const STROKES = { points: false, lines: true };
const DOTS = { points: true, lines: false };
const NONE = { points: false, lines: false };

test('an unset flag counts as shown; a set one hides as the renderer reads it', () => {
  assert.deepEqual(shownMarks({}), BOTH);
  assert.deepEqual(shownMarks({ showPoints: false }), STROKES);
  assert.deepEqual(shownMarks({ showLines: false }), DOTS);
  assert.deepEqual(shownMarks({ showPoints: false, showLines: false }), NONE);
  assert.deepEqual(shownMarks({ showPoints: null, showLines: 0 }), NONE, 'drawn as nothing, hit as nothing');
});

test('with both shown, lineAt is findLineAt itself', () => {
  for (const [x, y] of [[50, 5], [110, -5], [301, 301], [200, 200], [100, 50]])
    assert.equal(lineAt(LINES, BOTH, x, y, 8), findLineAt(LINES, x, y, 8), `at ${x},${y}`);
  assert.equal(lineAt(LINES, BOTH, 110, -5, 8), 0, 'a vertex reaches past its stroke (threshold + 4)');
});

test('points hidden: the stroke alone — no vertex pad, no lone point', () => {
  assert.equal(lineAt(LINES, STROKES, 50, 5, 8), 0, 'on the stroke');
  assert.equal(lineAt(LINES, STROKES, 110, -5, 8), -1, 'the hidden vertex reaches no further than the stroke');
  assert.equal(lineAt(LINES, STROKES, 301, 301, 8), -1, 'a lone point draws nothing, so it is never hit');
});

test('lines hidden: the points alone — a segment body is empty canvas', () => {
  assert.equal(lineAt(LINES, DOTS, 50, 3, 8), -1, 'the hidden stroke');
  assert.equal(lineAt(LINES, DOTS, 103, -4, 8), 0, 'a shown point names its line');
  assert.equal(lineAt(LINES, DOTS, 301, 301, 8), 1, 'and so does a lone point');
});

test('both hidden: nothing on the canvas is hit', () => {
  for (const [x, y] of [[50, 0], [100, 0], [300, 300]]) assert.equal(lineAt(LINES, NONE, x, y, 8), -1);
});

test('a hold never continues a hidden point nor inserts into a hidden line', () => {
  const at = (shown, x, y) => holdTargetAt(LINES, shown, x, y);
  assert.deepEqual(at(BOTH, 102, 1), { kind: 'point', lineIdx: 0, ptIdx: 1, ptIdx2: -1 });
  assert.equal(at(STROKES, 102, 1).kind, 'segment', 'over a hidden vertex the shown stroke is the target');
  assert.equal(at(STROKES, 301, 301).kind, 'new', 'a hidden lone point is empty canvas');
  assert.equal(at(BOTH, 50, 3).kind, 'segment');
  assert.equal(at(DOTS, 50, 3).kind, 'new', 'over a hidden stroke a hold starts afresh');
  assert.equal(at(DOTS, 102, 1).kind, 'point');
  assert.equal(at(NONE, 102, 1).kind, 'new');
});

// The defaults DrawingApp takes on (core/app/editing.js), on an app holding an in-progress stroke.
const hitApp = (showPoints, showLines) => {
  const app = { lines: LINES, currentLine: { points: pts([500, 500], [520, 500]) }, scale: 1, showPoints, showLines };
  for (const m of ['findLineAt', 'findNearestPoint', 'findNearestPointWithIdx', 'findNearestSegmentWithIdx'])
    app[m] = EditingMethods.prototype[m];
  return app;
};

test('the app answers no point while points are hidden, the in-progress stroke included', () => {
  const shown = hitApp(true, true);
  assert.ok(shown.findNearestPoint(100, 2));
  assert.deepEqual(shown.findNearestPointWithIdx(501, 500)?.lineIdx, -1);
  const hidden = hitApp(false, true);
  assert.equal(hidden.findNearestPoint(100, 2), null);
  assert.equal(hidden.findNearestPointWithIdx(100, 2), null);
  assert.equal(hidden.findNearestPointWithIdx(501, 500), null, 'nor a vertex of the stroke being drawn');
  assert.ok(hidden.findNearestSegmentWithIdx(50, 3), 'the stroke still is');
  assert.equal(hidden.findLineAt(50, 3), 0);
});

test('the app answers no segment while lines are hidden, and nothing with both hidden', () => {
  const dots = hitApp(true, false);
  assert.equal(dots.findNearestSegmentWithIdx(50, 3), null);
  assert.equal(dots.findLineAt(50, 3), -1);
  assert.ok(dots.findNearestPointWithIdx(100, 2), 'the points still are');
  const none = hitApp(false, false);
  assert.equal(none.findNearestPointWithIdx(100, 0), null);
  assert.equal(none.findNearestSegmentWithIdx(50, 0), null);
  assert.equal(none.findLineAt(100, 0), -1);
  assert.equal(none.findNearestPoint(300, 300), null);
});
