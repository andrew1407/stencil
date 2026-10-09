// A line's own `hidden` and `name` (core/models.hpp Line): a layout keeps both through its whitelist,
// a hidden line is out of every hit-test and every resting paint while its index stays, and rename
// and hide are one history step each. Desktop twin: tests/model/hiddenLines.headless.cpp.
import test from 'node:test';
import assert from 'node:assert/strict';

import { sanitizeLines } from '../../js/core/layout.js';
import { hittableLines, lineAt, holdTargetAt } from '../../js/core/pointer/markHits.js';
import { paintRestingLines } from '../../js/core/draw/restingPaint.js';
import { EditingMethods } from '../../js/core/app/editing.js';
import { renameLine, setLineHidden, lineNameOf } from '../../js/core/line/selection.js';

const pts = (...xy) => xy.map(([x, y]) => ({ x, y }));
const BOTH = { points: true, lines: true };
// Two lines on the same stroke: the later one, hidden, would win every hit.
const lines = () => [{ points: pts([0, 0], [100, 0]) }, { points: pts([0, 0], [100, 0]), hidden: true, name: 'Top' }];

test('a layout keeps a name and a set hidden flag, capped and typed; an unset one stays absent', () => {
  const [a, b, c] = sanitizeLines([
    { points: [], name: 'Roof', hidden: 1 },
    { points: [], name: 7, hidden: false },
    { points: [], name: 'x'.repeat(300) },
  ]);
  assert.deepEqual([a.name, a.hidden], ['Roof', true]);
  assert.ok(!('name' in b) && !('hidden' in b), 'a non-string name and a false flag add nothing');
  assert.equal(c.name.length, 80);
});

test('a hidden line keeps its index but is never a target', () => {
  const l = lines();
  const hit = hittableLines(l);
  assert.equal(hit.length, 2);
  assert.equal(hit[1].points.length, 0);
  assert.equal(hittableLines(l.slice(0, 1))[0], l[0], 'with none hidden the same lines come back');
  assert.equal(lineAt(l, BOTH, 50, 1, 8), 0, 'the shown line under it answers');
  assert.equal(holdTargetAt(l, BOTH, 50, 1).lineIdx, 0);
  l[0].hidden = true;
  assert.equal(lineAt(l, BOTH, 50, 1, 8), -1);
  assert.equal(holdTargetAt(null, BOTH, 0, 0).kind, holdTargetAt([], BOTH, 0, 0).kind, 'no lines is no target');
});

test("the app's finders skip a hidden line", () => {
  const app = { lines: lines(), currentLine: null, scale: 1, showPoints: true, showLines: true };
  for (const m of ['findNearestPoint', 'findNearestPointWithIdx', 'findNearestSegmentWithIdx', 'findLineAt'])
    app[m] = EditingMethods.prototype[m];
  assert.equal(app.findLineAt(50, 1), 0);
  assert.equal(app.findNearestPointWithIdx(0, 0).lineIdx, 0);
  assert.equal(app.findNearestSegmentWithIdx(50, 1).lineIdx, 0);
  app.lines[0].hidden = true;
  assert.equal(app.findNearestPoint(0, 0), null);
  assert.equal(app.findNearestSegmentWithIdx(50, 1), null);
});

test('an export, a thumbnail and the co-edit result paint only shown lines', () => {
  const drawn = [];
  const r = { drawLine: (line) => drawn.push(line.name ?? 'shown'), drawPoint: (p) => drawn.push(p) };
  paintRestingLines(r, lines(), { showLines: true, showPoints: true, pointSize: 4 });
  assert.deepEqual(drawn, ['shown']);
  drawn.length = 0;
  paintRestingLines(r, lines(), { showLines: false, showPoints: true, pointSize: 4 });
  assert.equal(drawn.length, 2, 'the points of the shown line alone');
});

test('rename and hide are one step each, and none when nothing changes', () => {
  let saved = 0;
  const app = { lines: lines(), saveHistory() { saved++; }, renderer: { redraw() {} }, compareReadOnly: () => false };
  assert.equal(lineNameOf('  a b  '), 'a b');
  assert.equal(renameLine(app, 0, ' Ridge '), true);
  assert.equal(renameLine(app, 0, 'Ridge'), false);
  assert.equal(setLineHidden(app, 1, true), false, 'already hidden');
  assert.equal(setLineHidden(app, 1, false), true);
  assert.equal(renameLine(app, 1, ''), true);
  assert.deepEqual([app.lines[0].name, app.lines[1].hidden, app.lines[1].name, saved], ['Ridge', false, '', 3]);
  assert.equal(setLineHidden(app, 9, true), false, 'no such line');
});
