// Closing a shape, and the two ways back out of one (js/core/dragGestures.js ringPoints /
// openRingAt / unchainLine / pullOutPoint): the selection panel's Unchain, and Alt+Ctrl/⌘+drag,
// which pulls a new point out of the line and breaks the area open at the spot pulled. A rect is
// the same thing with no closing duplicate. Desktop twin: canvas/chainEdit.hpp.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import { ringPoints, openRingAt, unchainLine, pullOutPoint } from '../js/core/touch/dragGestures.js';
import { fillState } from '../js/core/layout.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const drawingAppJs = read('../js/core/drawingApp.js');
const pointerJs = read('../js/core/pointer/pointerController.js');
const inputJs = read('../js/core/pointer/inputController.js');
const shapeJs = read('../js/core/line/shapeBuilder.js');   // closing / inserting / rects
const panelJs = read('../js/ui/selectionPanel.js');
const binderJs = read('../js/ui/bindings/selectionPanel.js');

const P = (...xy) => xy.map(([x, y]) => ({ x, y }));
// A shape closed by clicking its first point: the closing DUPLICATE at the end.
const closedShape = () => ({ locked: true, points: P([0, 0], [10, 0], [10, 10], [0, 0]) });
// A rect: locked, four corners, no duplicate.
const rect = () => ({ locked: true, points: P([0, 0], [10, 0], [10, 10], [0, 10]) });

// ── The ring ────────────────────────────────────────────────────────────────

test('ringPoints drops the closing duplicate, and leaves a rect alone', () => {
  assert.equal(ringPoints(closedShape().points).length, 3);
  assert.equal(ringPoints(rect().points).length, 4, 'a rect closes without a duplicate');
  assert.deepEqual(ringPoints(P([1, 2], [3, 4])), P([1, 2], [3, 4]), 'an open line is its own ring');
  assert.deepEqual(ringPoints([]), [], 'and an empty one does not throw');
});

test('openRingAt re-roots the ring so the seam is where you pulled', () => {
  const opened = openRingAt(closedShape().points, 1);
  assert.deepEqual(opened, P([10, 0], [10, 10], [0, 0], [10, 0]),
    'starts at vertex 1, runs all the way round, ends on a copy of it');
  assert.equal(opened.length, 4, 'three ring points plus the free end');
  assert.deepEqual(openRingAt(closedShape().points, 0), P([0, 0], [10, 0], [10, 10], [0, 0]),
    'breaking at point 0 is the shape it already looked like — now open');
  assert.deepEqual(openRingAt(rect().points, 3), P([0, 10], [0, 0], [10, 0], [10, 10], [0, 10]));
});

test('openRingAt copies its points — the opened line never aliases the closed one', () => {
  const shape = closedShape();
  const opened = openRingAt(shape.points, 1);
  opened[0].x = 999;
  assert.equal(shape.points[1].x, 10, 'the original ring is untouched');
});

// ── Unchaining ──────────────────────────────────────────────────────────────

test('unchainLine turns an area back into an open line and clears its fill', () => {
  const shape = closedShape();
  shape.fillColor = '#3399ff';
  assert.equal(unchainLine(shape), true);
  assert.equal(shape.locked, false);
  assert.deepEqual(shape.points, P([0, 0], [10, 0], [10, 10]), 'the closing duplicate is gone');
  // An open line has no area to paint, so the fill goes to 'transparent' — the app's own "no fill",
  // which fillState reads as unchecked — and re-closing brings the Fill field up CLEARED.
  assert.equal(shape.fillColor, 'transparent');
  assert.equal(fillState(shape, '#3399ff').enabled, false, 'the panel shows no fill');
});

test('unchainLine opens a rect too, and refuses anything that is not an area', () => {
  const r = rect();
  assert.equal(unchainLine(r), true);
  assert.deepEqual(r.points, P([0, 0], [10, 0], [10, 10], [0, 10]), 'all four corners kept');
  assert.equal(unchainLine({ locked: false, points: P([0, 0], [1, 1]) }), false);
  assert.equal(unchainLine(null), false);
});

// ── Pulling a new point out ─────────────────────────────────────────────────

test('pull-out on an open line duplicates the vertex you grabbed', () => {
  const line = { locked: false, points: P([0, 0], [10, 0], [20, 0]) };
  const idx = pullOutPoint(line, { kind: 'point', ptIdx: 1 }, 11, 4);
  assert.equal(idx, 2, 'the copy sits right after the point it came from');
  assert.deepEqual(line.points, P([0, 0], [10, 0], [10, 0], [20, 0]));
});

test('pull-out on a segment body puts the new point under the cursor', () => {
  const line = { locked: false, points: P([0, 0], [20, 0]) };
  const idx = pullOutPoint(line, { kind: 'segment', ptIdx: 0, ptIdx2: 1 }, 9, 5);
  assert.equal(idx, 1);
  assert.deepEqual(line.points, P([0, 0], [9, 5], [20, 0]));
});

test('pull-out on an AREA breaks it open at the vertex pulled', () => {
  const shape = closedShape();
  shape.fillColor = '#3399ff';
  const idx = pullOutPoint(shape, { kind: 'point', ptIdx: 1 }, 11, 4);
  assert.equal(shape.locked, false, 'it stops being an area');
  assert.equal(shape.fillColor, 'transparent', 'and its fill goes with the shape');
  assert.equal(idx, shape.points.length - 1, 'the free end is what you drag');
  assert.deepEqual(shape.points, P([10, 0], [10, 10], [0, 0], [10, 0]),
    'the seam is at vertex 1, not at point 0');
});

test('pull-out on an area SEGMENT breaks it and the loose end follows the cursor', () => {
  const r = rect();
  const idx = pullOutPoint(r, { kind: 'segment', ptIdx: 1, ptIdx2: 2 }, 14, 6);
  assert.equal(r.locked, false);
  assert.deepEqual(r.points[idx], { x: 14, y: 6 }, 'the break follows the pointer');
  assert.deepEqual(r.points.slice(0, idx), P([10, 10], [0, 10], [0, 0], [10, 0]),
    'and the rest of the ring runs from the vertex it broke at');
});

test('pull-out declines when there is nothing under the cursor', () => {
  assert.equal(pullOutPoint(null, { kind: 'point', ptIdx: 0 }, 0, 0), -1);
  assert.equal(pullOutPoint({ points: [] }, null, 0, 0), -1);
  assert.equal(pullOutPoint({ points: P([0, 0]) }, { kind: 'point', ptIdx: 7 }, 0, 0), -1);
});

// ── Closing: every route, and a target you can actually hit ─────────────────

test('the close check is reached from BOTH routes, not just the click', () => {
  // Both routes go through tryCloseShapeAt: a hold-to-draw triangle ending on its own first point
  // must close, not stay an open line that merely looks closed (user report).
  assert.match(drawingAppJs, /tryCloseShapeAt\(x, y\)/, 'the click path asks');
  assert.match(inputJs, /app\.tryCloseShapeAt\(x, y\)/, 'and so does the hold-to-draw drop');
  assert.equal((shapeJs.match(/shouldCloseShape\(/g) || []).length, 2,
    'the raw check (lineTransforms.js) has ONE caller here — tryCloseShapeAt, twice');
  // Closing ends the gesture: the stroke is committed, so nothing may drop into it after.
  // From the DEFINITION, not the call site in #holdTick that appears earlier.
  const drop = inputJs.slice(inputJs.indexOf('#holdDrop(clientX, clientY)'),
                             inputJs.indexOf('#holdCommit() {'));
  assert.ok(drop.indexOf('tryCloseShapeAt') < drop.indexOf('continueInsertIdx'),
    'the close is tried BEFORE the point is added');
  assert.match(drop, /this\.#holdDraw\.cancel\(\)/, 'and the hold gesture is ended');
});

test('closing a shape selects nothing — it ends like an ordinary line', () => {
  // Closing a shape selects nothing, exactly as finishing an ordinary line (stopDrawingMode) does:
  // the coordinate table follows it, the selected-line bar stays away.
  const close = shapeJs.slice(shapeJs.indexOf('closeShape = (app, { line, idx, isContinuation })'),
                              shapeJs.indexOf('insertPointOnSegment = (app, lineIdx'));
  assert.ok(!/app\.selectedLineIdx = areaIdx/.test(close), 'it does not select the new area');
  assert.ok(!/app\.focusedPtIdx = -1/.test(close), 'and focuses no point');
  assert.match(close, /app\.coordLineIdx = areaIdx/, 'the coordinate table still follows it');
  // The one showSelectionPanel left is a REFRESH of an already-open bar (a continued shape
  // was drawn on a selected line), never an opening.
  assert.match(close, /if \(app\.selectedLineIdx === areaIdx\) app\.showSelectionPanel/);
  assert.equal((close.match(/showSelectionPanel/g) || []).length, 1);
});

test('a dwell-closed shape swallows the click its own RELEASE leaves behind', () => {
  // The guard is armed on the RELEASE, since the gesture ends at the dwell well before the button
  // comes up — the bar pushes the canvas down, so a trailing click would land elsewhere.
  const drop = inputJs.slice(inputJs.indexOf('#holdDrop(clientX, clientY)'),
                             inputJs.indexOf('#suppressTrailingClick()'));
  assert.match(drop, /#holdClosedShape = true/, 'the close marks the release as owing a click');
  assert.ok(!/dragJustEnded/.test(drop), 'and does NOT arm the guard from the dwell');
  // Every release route honours it, BEFORE the "gesture already idle" early-out.
  const mouseUp = inputJs.slice(inputJs.indexOf("addEventListener('mouseup'"),
                                inputJs.indexOf("window.addEventListener('blur'"));
  assert.ok(mouseUp.indexOf('#holdClosedShape') < mouseUp.indexOf("ctrl.state === 'idle'"),
    'the mouse release checks it first');
  assert.match(inputJs, /if \(this\.#holdClosedShape\) \{ this\.#holdReleaseAfterClose\(\); this\.#touch = null; return; \}/,
    'and so does the touch release');
  // One place arms the guard, so the two release paths cannot drift.
  assert.equal((inputJs.match(/dragJustEnded = true/g) || []).length, 1,
    'exactly one arming site (#suppressTrailingClick)');
});

test('the close grab is a constant size ON SCREEN, not in image pixels', () => {
  // The grab radius divides by the zoom, like every other hit test here: `pointSize + 8` image px is
  // ~3 screen px at 25%. The desktop's headless twin drives real clicks at 25%.
  const fn = shapeJs.slice(shapeJs.indexOf('closeGrabSize = (app, line) => {'),
                           shapeJs.indexOf('tryCloseShapeAt = (app, x, y) => {'));
  assert.match(fn, /app\.scale/, 'it reads the zoom');
  assert.match(fn, /\/ scale/, 'and divides by it');
  assert.match(fn, /Math\.max\(ps,/, 'never tighter than the honest image-space distance');
  assert.match(fn, /const slack = CLOSE_SLACK;/, "core's own slack is named, not repeated as a literal");
  // Both close routes size their grab this way — neither passes a raw pointSize.
  const tryClose = shapeJs.slice(shapeJs.indexOf('tryCloseShapeAt = (app, x, y) => {'),
                                 shapeJs.indexOf('closeCurrentShape = (app) => {'));
  assert.equal((tryClose.match(/closeGrabSize\(/g) || []).length, 2,
    'the continued stroke and the fresh one both get the zoom-aware grab');
  assert.ok(!/pointSize \?\? app\.pointSize\)\)/.test(tryClose), 'no raw pointSize left');
});

// ── The wiring ──────────────────────────────────────────────────────────────

test('the pull-out chord opens no context menu', () => {
  // On macOS Ctrl+click IS the secondary click, so the Alt+Ctrl drag has to suppress `contextmenu`
  // while a plain Ctrl+click still gets its menu (user report).
  const ctxJs = readFileSync(new URL('../js/ui/contextMenu/contextMenu.js', import.meta.url), 'utf8');
  const handler = ctxJs.slice(ctxJs.indexOf("el.addEventListener('contextmenu'"),
                              ctxJs.indexOf('// Close on outside click'));
  assert.match(handler, /if \(e\.altKey\) return;/, 'Alt means the gesture, not a menu');
  assert.ok(handler.indexOf('e.preventDefault()') < handler.indexOf('if (e.altKey) return;'),
    'the native menu is suppressed either way — the gesture must not raise one of those');
  assert.ok(handler.indexOf('if (e.altKey) return;') < handler.indexOf('openAt('),
    'and our own menu never opens for it');
});

test('Alt+Ctrl/⌘+drag is checked before the plain Alt gestures', () => {
  // Left to the plain Alt branch, the press would MOVE the point already there instead
  // of pulling a new one out of it.
  const pull = pointerJs.indexOf('beginPullOutDrag');
  const altShift = pointerJs.indexOf('e.altKey && e.shiftKey');
  assert.ok(pull > 0 && altShift > 0 && pull < altShift, 'the pull-out must be tested first');
  assert.match(pointerJs, /e\.altKey && \(e\.ctrlKey \|\| e\.metaKey\) && !e\.shiftKey/);
});

test('the pull-out selects the line it broke and drags the new point', () => {
  assert.match(drawingAppJs, /this\.isDraggingPoint = true;\s*\n\s*this\.draggingPoint = \{ lineIdx: target\.lineIdx, ptIdx: idx \}/);
  assert.match(drawingAppJs, /if \(this\.compareReadOnly\(\)\) return false;/, 'never in a read-only compare view');
});

test('Unchain is offered exactly where a line is an area', () => {
  // #sel-fill-group is already shown only for locked lines (showSelectionPanel), so the
  // button inside it needs no gate of its own.
  const group = panelJs.slice(panelJs.indexOf('id="sel-fill-group"'), panelJs.indexOf('id="sel-deselect"'));
  assert.ok(group.includes('id="sel-unchain"'), 'the button lives in the area-only group');
  assert.match(binderJs, /getElementById\('sel-unchain'\)\.addEventListener\('click', \(\) => app\.unchainSelectedLine\(\)\)/);
  assert.match(drawingAppJs, /unchainSelectedLine\(\)/);
});
