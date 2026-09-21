// The stroke-growth wiring: every route that adds a point sends it flying, a restore grounds
// every flight, and an export is the resting picture. Split from strokeGrowth.test.js.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import { recordingCtx, argsOf, indexOf } from './helpers/recordingCtx.js';
import { harness, lineOf } from './helpers/strokeGrowthHarness.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const drawingAppJs = read('../js/core/drawingApp.js');
const inputJs = read('../js/core/pointer/inputController.js');
const shapeJs = read('../js/core/line/shapeBuilder.js');
const clickJs = read('../js/core/pointer/canvasClick.js');
const exportJs = read('../js/core/export/exportService.js');

// ── 3. The wiring ───────────────────────────────────────────────────────────

test('every route that adds a point sends it flying', () => {
  // Five click/insert routes, plus the Alt+Ctrl pull-out, which adds a point too.
  const flights = (src) => (src.match(/(this|app)\.strokeFx\.flyIn\(/g) || []).length;
  assert.equal(flights(drawingAppJs) + flights(shapeJs) + flights(clickJs), 6, 'every route');
  assert.match(drawingAppJs, /this\.strokeFx\.flyIn\(line, idx, \{ x, y \}\)/,
    'a pulled-out point flies out of the spot it was pulled from');
  // …and all THREE rect routes: appended to a continued line, appended to the selected
  // line, and a standalone one drawn on empty space.
  assert.equal((shapeJs.match(/app\.strokeFx\.flyInRange\(/g) || []).length, 3,
    'shapeBuilder: every rect draws itself corner by corner');
  assert.match(shapeJs, /app\.strokeFx\.flyInRange\(rect, 0, corners\.length\)/,
    'a standalone rect starts at its own first corner');
  assert.match(shapeJs, /flyIn\(line, insertIdx, strokeFoot\(/,
    'an inserted vertex comes out of its foot on the segment');
  // the hold-draw seed, a hold drop on a continued line, and one on a fresh stroke
  assert.equal((inputJs.match(/app\.strokeFx\.flyIn\(/g) || []).length, 3,
    'inputController: the hold-to-draw and touch drops');
});

test('a restored or wiped set of lines grounds every flight', () => {
  // A flight holds the point OBJECT it belongs to; undo/redo swap in a snapshot's own
  // points and a wipe drops them all, so nothing in the air still has a vertex to be.
  assert.equal((drawingAppJs.match(/this\.strokeFx\.cancel\(\)/g) || []).length, 3,
    'undo, redo and clear-all');
  const h = harness();
  const line = lineOf([0, 0], [100, 0]);
  h.fx.flyIn(line, 1);
  h.fx.cancel();
  assert.equal(h.fx.active, false);
  assert.equal(h.fx.pointsOf(line), line.points);
});

test('the renderer draws the flown positions, at their flown size', async () => {
  const { Renderer } = await import('../js/core/draw/renderer.js');
  const { ctx, calls } = recordingCtx();
  const line = lineOf([0, 0], [100, 0]);
  const flown = [{ x: 7, y: 8 }, { x: 40, y: 9 }];
  const seen = [];
  const at = (tag) => (c, l, pts) => seen.push([tag, c, l, pts, calls.length]);
  const fx = { pointsOf: () => flown, scaleAt: () => 3, paintUnder: at('under'), paintOver: at('over') };
  new Renderer({ ctx, strokeFx: fx, showPoints: true, pointSize: 4, listHoverLineIdx: -1 })
    .drawLine(line, false, 0);
  // A geometry pass that read the resting array, or a radius that skipped scaleAt, lands here.
  const geom = [...argsOf(calls, 'moveTo'), ...argsOf(calls, 'lineTo')];
  assert.equal(geom.length, line.points.length, 'one stroke pass, every vertex');
  for (const [x, y] of geom) assert.ok(flown.some((f) => f.x === x && f.y === y),
    `drew (${x},${y}) — no geometry pass may read the resting array`);
  assert.deepEqual(argsOf(calls, 'arc').map((a) => a[2]), [12, 12], 'pointSize x scaleAt');
  const stroke = indexOf(calls, 'stroke');
  assert.deepEqual(seen.map(([tag, c, l, pts]) => [tag, c, l, pts]),
    [['under', ctx, line, flown], ['over', ctx, line, flown]], 'both overlays, on the app ctx');
  assert.ok(seen[0][4] <= stroke && seen[1][4] > stroke, 'the wake under the stroke, the spark over');
});

test('an export is the resting picture — never a vertex caught mid-air', () => {
  assert.match(exportJs, /app\.strokeFx\.suspend\(\)/);
  assert.match(exportJs, /app\.strokeFx\.resume\(\)/, 'and it is put back afterwards');
});
