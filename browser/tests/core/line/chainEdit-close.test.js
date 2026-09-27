// Closing a shape (js/core/line/shapeBuilder.js tryCloseShapeAt / closeShape), driven through both
// routes that reach it — a click, and a hold-to-draw dwell (inputController.js) — on stub apps.
// Desktop twin: canvas/draw/CanvasDrawClick.cpp tryCloseShapeAt.
import test, { afterEach } from 'node:test';
import assert from 'node:assert';
import { installDom } from '../../helpers/dom.js';
import constants from '../../../js/config/constants.json' with { type: 'json' };

const doc = installDom({}, { window: { addEventListener() {} } });
const shapeBuilder = await import('../../../js/core/line/shapeBuilder.js');
const { canvasClick } = await import('../../../js/core/pointer/canvasClick.js');
const { InputController } = await import('../../../js/core/pointer/inputController.js');
const SLACK = constants.HIT.closeSlackPx;
const real = { now: performance.now, setInterval, clearInterval, setTimeout };
afterEach(() => {
  performance.now = real.now;
  Object.assign(globalThis, { setInterval: real.setInterval, clearInterval: real.clearInterval, setTimeout: real.setTimeout });
});

const P = (...xy) => xy.map(([x, y]) => ({ x, y }));
const stubApp = (over = {}) => {
  const seen = { panels: 0, coord: [] };
  const app = {
    seen, lines: [], currentLine: null, isDrawing: true, drawMode: 'line', image: {}, scale: 1, pointSize: 4,
    selectedLineIdx: -1, coordLineIdx: -1, focusedPtIdx: 2, continueLineIdx: -1, continueInsertIdx: -1,
    holdDrawDelay: 500, undonePoints: [],
    // Client px are image px: the canvas sits at the origin at 1:1 (pointer/canvasCoords.js).
    canvas: { width: 1000, height: 1000, style: {}, addEventListener() {},
      getBoundingClientRect: () => ({ left: 0, top: 0, width: 1000, height: 1000 }) },
    strokeFx: { flyIn() {}, flyInRange() {} }, renderer: { redraw() {}, requestRedraw() {} }, coordTable: { update: (...a) => seen.coord.push(a) },
    showSelectionPanel() { seen.panels++; }, hideSelectionPanels() {}, saveHistory() {},
    compareReadOnly: () => false,
    ...over,
  };
  return app;
};
const triangle = () => ({ points: P([0, 0], [100, 0], [100, 100]) });
const closed = (line) => line.locked === true && line.points.length === 4 && line.points[3].x === 0 && line.points[3].y === 0;

// A hold-to-draw gesture on a controllable clock: `dwell(x, y)` moves there and rests a hold long.
const holdRig = (app) => {
  let t = 0;
  let tick = () => {};
  performance.now = () => t;
  globalThis.setInterval = (fn) => { tick = fn; return 1; };
  globalThis.clearInterval = () => {};
  const timers = [];
  globalThis.setTimeout = (fn) => timers.push(fn);
  const ic = new InputController(app);
  ic.wireHoldDraw();
  app.isDrawing = false;
  return {
    ic, timers,
    press: (x, y) => { t = 0; ic.armHold(x, y); t = 500; tick(); },
    dwell: (x, y) => { t += 100; doc.dispatch('mousemove', { clientX: x, clientY: y }); t += 500; tick(); },
    release: () => doc.dispatch('mouseup', {}),
  };
};

test('the close check is reached from BOTH routes, not just the click', () => {
  // Both routes go through tryCloseShapeAt: a hold-to-draw triangle ending on its own first point
  // must close, not stay an open line that merely looks closed (user report).
  const click = stubApp({ currentLine: triangle() });
  canvasClick(click, { clientX: 1, clientY: 1 });
  assert.ok(click.lines.length === 1 && closed(click.lines[0]), 'the click path closes it');
  const held = stubApp();
  const g = holdRig(held);
  g.press(0, 0);
  g.dwell(100, 0);
  g.dwell(100, 100);
  g.dwell(1, 1);
  assert.strictEqual(held.lines.length, 1, 'and so does the hold-to-draw drop');
  assert.ok(closed(held.lines[0]), 'the close is tried BEFORE the point is added');
  // Closing ends the gesture: the stroke is committed, so nothing may drop into it after.
  assert.strictEqual(g.ic.holdEngaged, false, 'and the hold gesture is ended');
  g.dwell(60, 60);
  assert.deepStrictEqual([held.lines.length, held.lines[0].points.length, held.currentLine], [1, 4, null]);
});

test('closing a shape selects nothing — it ends like an ordinary line', () => {
  // Closing a shape selects nothing, exactly as finishing an ordinary line (stopDrawingMode) does:
  // the coordinate table follows it, the selected-line bar stays away.
  const fresh = stubApp({ lines: [{ points: P([5, 5]) }], currentLine: triangle() });
  assert.strictEqual(shapeBuilder.tryCloseShapeAt(fresh, 0, 0), true);
  assert.strictEqual(fresh.selectedLineIdx, -1, 'it does not select the new area');
  assert.strictEqual(fresh.focusedPtIdx, 2, 'and focuses no point');
  assert.strictEqual(fresh.coordLineIdx, 1, 'the coordinate table still follows it');
  assert.deepStrictEqual(fresh.seen.coord, [[fresh.lines[1].points, 1]]);
  assert.strictEqual(fresh.seen.panels, 0, 'no bar opens');
  // The one showSelectionPanel left is a REFRESH of an already-open bar (a continued shape
  // was drawn on a selected line), never an opening.
  for (const [selected, panels] of [[0, 1], [-1, 0]]) {
    const cont = stubApp({ lines: [triangle()], continueLineIdx: 0, continueInsertIdx: 3, selectedLineIdx: selected });
    assert.strictEqual(shapeBuilder.tryCloseShapeAt(cont, 0, 0), true);
    assert.ok(closed(cont.lines[0]) && cont.continueLineIdx === -1);
    assert.strictEqual(cont.seen.panels, panels, `selected ${selected}: the bar is refreshed only if it is up`);
  }
});

test('a dwell-closed shape swallows the click its own RELEASE leaves behind', () => {
  // The guard is armed on the RELEASE, since the gesture ends at the dwell well before the button
  // comes up — the bar pushes the canvas down, so a trailing click would land elsewhere.
  const closeByDwell = () => {
    const app = stubApp();
    const g = holdRig(app);
    g.press(0, 0);
    g.dwell(100, 0);
    g.dwell(100, 100);
    g.dwell(1, 1);
    assert.ok(closed(app.lines[0]));
    assert.ok(!app.dragJustEnded, 'the dwell does NOT arm the guard');
    return { app, g };
  };
  const mouse = closeByDwell();
  mouse.g.release();
  assert.strictEqual(mouse.app.dragJustEnded, true, 'the mouse release checks it first');
  mouse.g.timers.forEach((fn) => fn());
  assert.strictEqual(mouse.app.dragJustEnded, false, 'and the guard lapses after the click');
  const touch = closeByDwell();
  let tapped = 0;
  touch.g.ic.touchSession = { moved: 0, startT: 0 };
  touch.g.ic.endTapGesture(touch.g.ic.touchSession, () => { tapped++; });
  assert.deepStrictEqual([touch.app.dragJustEnded, tapped, touch.g.ic.touchSession], [true, 0, null],
    'and so does the touch release');
});

test('the close grab is a constant size ON SCREEN, not in image pixels', () => {
  // The grab radius divides by the zoom, like every other hit test here: `pointSize + 8` image px is
  // ~3 screen px at 25%. The desktop's headless twin drives real clicks at 25%.
  const line = { points: [] };
  for (const scale of [0.25, 0.5, 0.8]) {
    const grab = shapeBuilder.closeGrabSize({ scale, pointSize: 4 }, line);
    assert.ok(Math.abs((grab + SLACK) * scale - (4 + SLACK)) < 1e-9, `the same screen radius at ${scale}`);
  }
  assert.strictEqual(shapeBuilder.closeGrabSize({ scale: 3, pointSize: 4 }, line), 4,
    'never tighter than the honest image-space distance');
  assert.strictEqual(shapeBuilder.closeGrabSize({ scale: 0.5, pointSize: 4 }, { points: [], pointSize: 6 }),
    (6 + SLACK) / 0.5 - SLACK, "the line's own point size wins over the app's");
  // Both close routes size their grab this way — neither passes a raw pointSize.
  const far = 30;
  const fresh = stubApp({ scale: 0.25, currentLine: triangle() });
  assert.strictEqual(shapeBuilder.tryCloseShapeAt(fresh, far, 0), true, 'a fresh stroke closes 30 image px out at 25%');
  const cont = stubApp({ scale: 0.25, lines: [triangle()], continueLineIdx: 0, continueInsertIdx: 3 });
  assert.strictEqual(shapeBuilder.tryCloseShapeAt(cont, far, 0), true, 'and so does a continued one');
  assert.strictEqual(shapeBuilder.tryCloseShapeAt(stubApp({ scale: 1, currentLine: triangle() }), far, 0), false,
    'at 100% the same spot is too far');
});
