// The stroke-growth wiring: every route that adds a point sends it flying, a restore grounds
// every flight, and an export is the resting picture. Each route is driven on a stub app whose
// strokeFx records the point objects it was handed.
import test from 'node:test';
import assert from 'node:assert';

import { recordingCtx, argsOf, indexOf } from '../../helpers/recordingCtx.js';
import { harness, lineOf } from '../../helpers/strokeGrowthHarness.js';
import { ExportService, makeApp } from '../../helpers/exportServiceRig.js';
import * as shapeBuilder from '../../../js/core/line/shapeBuilder.js';
import { canvasClick } from '../../../js/core/pointer/canvasClick.js';
import { EditingMethods } from '../../../js/core/app/editing.js';
import { InputController } from '../../../js/core/pointer/inputController.js';
import { strokeFoot } from '../../../js/ui/motion.js';

const recorder = () => {
  const flights = [];
  const record = (line, idx, n, from) => flights.push({ line, idx, n, from, pts: line.points.slice(idx, idx + n) });
  return { flights, cancels: 0, flyIn: (l, i, from) => record(l, i, 1, from),
           flyInRange: (l, i, n) => record(l, i, n), cancel() { this.cancels++; } };
};
const pts = (...xy) => xy.map(([x, y]) => ({ x, y }));
const stubApp = (over = {}) => ({
  lines: [], currentLine: null, selectedLineIdx: -1, coordLineIdx: -1, focusedPtIdx: -1,
  continueLineIdx: -1, continueInsertIdx: -1, isDrawing: false, drawMode: 'line', image: {},
  color: '#f00', thickness: 2, pointSize: 4, style: 'solid', holdDrawDelay: 500,
  strokeFx: recorder(), coordTable: { update() {} }, renderer: { redraw() {}, requestRedraw() {}, effectiveCompareMode: () => 'none' },
  showSelectionPanel() {}, hideSelectionPanels() {}, saveHistory() {}, compareReadOnly: () => false,
  // Client px are image px: the canvas sits at the origin at 1:1 (pointer/canvasCoords.js).
  canvas: { width: 1000, height: 1000, style: {}, getBoundingClientRect: () => ({ left: 0, top: 0, width: 1000, height: 1000 }) },
  ...over,
});
// Every point object the route added must be one it sent flying.
const routeFlies = (name, app, act) => {
  const all = () => [...app.lines, app.currentLine].filter(Boolean).flatMap((l) => l.points);
  const before = new Set(all());
  act();
  const fresh = all().filter((p) => !before.has(p));
  assert.ok(fresh.length > 0, `${name}: the route added a point`);
  const air = new Set(app.strokeFx.flights.flatMap((f) => f.pts));
  for (const p of fresh) assert.ok(air.has(p), `${name}: the point at (${p.x},${p.y}) never flew`);
  return app.strokeFx.flights;
};

test('every route that adds a point sends it flying', () => {
  const fresh = stubApp({ isDrawing: true, currentLine: { points: pts([0, 0]) } });
  routeFlies('click on a fresh stroke', fresh, () => canvasClick(fresh, { clientX: 30, clientY: 40 }));
  const cont = stubApp({ isDrawing: true, lines: [{ points: pts([0, 0], [50, 0]) }], continueLineIdx: 0, continueInsertIdx: 2 });
  routeFlies('click on a continued line', cont, () => canvasClick(cont, { clientX: 90, clientY: 9 }));
  const ins = stubApp({ lines: [{ points: pts([0, 0], [100, 0]) }] });
  const [insert] = routeFlies('insert on a segment', ins, () => shapeBuilder.insertPointOnSegment(ins, 0, 1, 50, 20));
  assert.deepStrictEqual(insert.from, strokeFoot({ x: 0, y: 0 }, { x: 100, y: 0 }, 50, 20),
    'an inserted vertex comes out of its foot on the segment');
  const joined = stubApp({ lines: [{ points: pts([0, 0], [10, 0]) }], selectedLineIdx: 0 });
  routeFlies('a point joined to the selected line', joined, () => shapeBuilder.addConnectedPoint(joined, 20, 5));
  const lone = stubApp();
  routeFlies('a standalone point', lone, () => shapeBuilder.addConnectedPoint(lone, 20, 5));
  const pull = stubApp({ lines: [{ points: pts([0, 0], [100, 0]) }], findNearestPointWithIdx: () => ({ lineIdx: 0, ptIdx: 1 }) });
  const [pulled] = routeFlies('the Alt+Ctrl pull-out', pull, () => EditingMethods.prototype.beginPullOutDrag.call(pull, 97, 3));
  assert.deepStrictEqual(pulled.from, { x: 97, y: 3 }, 'a pulled-out point flies out of the spot it was pulled from');
});

test('every rect route draws itself corner by corner', () => {
  const cont = stubApp({ lines: [{ points: pts([0, 0], [5, 5]) }], continueLineIdx: 0, continueInsertIdx: 2 });
  routeFlies('a rect appended to a continued line', cont, () => shapeBuilder.createRect(cont, 10, 10, 40, 30));
  const joined = stubApp({ lines: [{ points: pts([0, 0], [5, 5]) }], selectedLineIdx: 0 });
  routeFlies('a rect appended to the selected line', joined, () => shapeBuilder.createRect(joined, 10, 10, 40, 30, true));
  const lone = stubApp();
  const [flight] = routeFlies('a standalone rect', lone, () => shapeBuilder.createRect(lone, 10, 10, 40, 30));
  assert.deepStrictEqual([flight.line, flight.idx, flight.n], [lone.lines[0], 0, 4],
    'a standalone rect starts at its own first corner');
});

// The hold-to-draw seed, a hold drop on a continued line, and one on a fresh stroke.
test('the hold-to-draw seed and both hold drops send their points flying', () => {
  const saved = { now: performance.now, setInterval: globalThis.setInterval, clearInterval: globalThis.clearInterval, window: globalThis.window };
  let t = 0;
  let tick = null;
  performance.now = () => t;
  Object.assign(globalThis, { setInterval: (fn) => { tick = fn; return 1; }, clearInterval() {}, window: { addEventListener() {} } });
  const hold = (app, [x, y], [mx, my]) => {
    app.canvas = { ...app.canvas, addEventListener() {} };
    const ic = new InputController(app);
    ic.wireHoldDraw();
    t = 0;
    ic.armHold(x, y);
    return { start: () => { t = 500; tick(); },
             drop: () => { t = 600; document.dispatch('mousemove', { clientX: mx, clientY: my }); t = 1100; tick(); } };
  };
  try {
    const fresh = stubApp();
    const g = hold(fresh, [100, 100], [200, 100]);
    routeFlies('the hold-draw seed', fresh, g.start);
    routeFlies('a hold drop on a fresh stroke', fresh, g.drop);
    const cont = stubApp({ lines: [{ points: pts([0, 0], [50, 0]) }] });
    const c = hold(cont, [50, 0], [150, 50]);
    c.start();
    routeFlies('a hold drop on a continued line', cont, c.drop);
  } finally {
    performance.now = saved.now;
    Object.assign(globalThis, { setInterval: saved.setInterval, clearInterval: saved.clearInterval, window: saved.window });
  }
});

test('a restored or wiped set of lines grounds every flight', async () => {
  // A flight holds the point OBJECT it belongs to; undo/redo swap in a snapshot's own
  // points and a wipe drops them all, so nothing in the air still has a vertex to be.
  const snapshot = [{ points: pts([1, 1]) }];
  const app = Object.assign(Object.create(EditingMethods.prototype), stubApp({
    history: { undo: () => snapshot, redo: () => snapshot }, imageModel: { restoreView: () => false },
  }));
  app.undo();
  assert.strictEqual(app.strokeFx.cancels, 1, 'undo restores through the grounding step');
  assert.strictEqual(app.lines, snapshot);
  app.redo();
  assert.strictEqual(app.strokeFx.cancels, 2, 'and so does redo');
  globalThis.location ??= { hash: '', pathname: '/app', search: '' };
  globalThis.history ??= { replaceState() {} };
  const { DrawingApp } = await import('../../../js/core/drawingApp.js');
  const wiped = stubApp({ lines: [{ points: pts([0, 0], [9, 9]) }], confirm: async () => true, hideSelectionPanels() {} });
  await DrawingApp.prototype.clearAllLines.call(wiped);
  assert.deepStrictEqual([wiped.lines, wiped.strokeFx.cancels], [[], 1], 'clear-all grounds them too');
  const h = harness();
  const line = lineOf([0, 0], [100, 0]);
  h.fx.flyIn(line, 1);
  h.fx.cancel();
  assert.equal(h.fx.active, false);
  assert.equal(h.fx.pointsOf(line), line.points);
});

test('the renderer draws the flown positions, at their flown size', async () => {
  const { Renderer } = await import('../../../js/core/draw/renderer.js');
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
  const drawn = [];
  const fx = { suspended: false, suspend() { this.suspended = true; }, resume() { this.suspended = false; } };
  const app = makeApp({
    image: {}, showLines: true, lines: [lineOf([0, 0], [9, 9]), lineOf([1, 1], [5, 5])], strokeFx: fx,
    renderer: { drawImageWithFilter() {}, drawLine: () => drawn.push(fx.suspended) },
  });
  new ExportService(app).renderExportCanvas('current');
  assert.deepStrictEqual(drawn, [true, true], 'every line is drawn with the flights suspended');
  assert.strictEqual(fx.suspended, false, 'and it is put back afterwards');
});
