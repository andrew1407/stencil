// stencil.line / stencil.point (js/console/stencilApi.js): the transforms, the join, the
// per-point writes and the individual setters that commit each change.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, makeApp, called, lastCall } from './helpers/stencilApiRig.js';

// ── Lines ───────────────────────────────────────────────────────────────────────
test('line.move translates every point and commits (history + redraw + coord table)', () => {
  const app = makeApp({ lines: [{ color: '#111111', thickness: 1, points: [{ x: 10, y: 20 }, { x: 30, y: 40 }] }] });
  const stencil = createStencil(app);

  const line = stencil.lines[0];
  assert.equal(line.move({ x: 5, y: -5 }), line);
  assert.deepEqual(app.lines[0].points, [{ x: 15, y: 15 }, { x: 35, y: 35 }]);
  assert.equal(called(app, 'saveHistory').length, 1);
  assert.equal(called(app, 'redraw').length, 1);
  assert.equal(called(app, 'coordTableUpdate').length, 1);
});

test('line.rotate rotates points around the bbox centre by default', () => {
  const app = makeApp({ lines: [{ points: [{ x: 0, y: 0 }, { x: 10, y: 0 }] }] });
  const stencil = createStencil(app);

  stencil.lines[0].rotate(90);   // centre = (5,0); 90° CW
  const [p0, p1] = app.lines[0].points;
  assert.ok(Math.abs(p0.x - 5) < 1e-9 && Math.abs(p0.y - (-5)) < 1e-9);
  assert.ok(Math.abs(p1.x - 5) < 1e-9 && Math.abs(p1.y - 5) < 1e-9);
});

test('flipH/flipV/rotate90/rotateMinus90 route to the selected-line transforms (chainable)', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.equal(stencil.flipH(), stencil);
  assert.deepEqual(lastCall(app, 'flipSelectedLine'), ['flipSelectedLine', true]);
  assert.equal(stencil.flipV(), stencil);
  assert.deepEqual(lastCall(app, 'flipSelectedLine'), ['flipSelectedLine', false]);
  assert.equal(stencil.rotate90(), stencil);
  assert.deepEqual(lastCall(app, 'rotateSelectedLineQuarter'), ['rotateSelectedLineQuarter', 1]);
  assert.equal(stencil.rotateMinus90(), stencil);
  assert.deepEqual(lastCall(app, 'rotateSelectedLineQuarter'), ['rotateSelectedLineQuarter', -1]);
});

test('line.apply batch-updates style props and normalizes color/fillColor', () => {
  const app = makeApp({ lines: [{ color: '#000000', thickness: 1, pointSize: 1, style: 'solid', fillColor: 'transparent', points: [] }] });
  const stencil = createStencil(app);

  stencil.lines[0].apply({ color: '#ABC', thickness: 3, pointSize: 7, style: 'dashed', fillColor: 'transparent' });
  const l = app.lines[0];
  assert.equal(l.color, '#aabbcc');
  assert.equal(l.thickness, 3);
  assert.equal(l.pointSize, 7);   // pointSize alias → pointSize
  assert.equal(l.style, 'dashed');
  assert.equal(l.fillColor, 'transparent');
});

test('line.add inserts a point at a neighbour slot; line.remove(index) drops one', () => {
  const app = makeApp({ lines: [{ points: [{ x: 0, y: 0 }, { x: 10, y: 10 }] }] });
  const stencil = createStencil(app);

  stencil.lines[0].add({ x: 5, y: 5 }, { neighbour: 0 });   // after index 0
  assert.deepEqual(app.lines[0].points, [{ x: 0, y: 0 }, { x: 5, y: 5 }, { x: 10, y: 10 }]);

  stencil.lines[0].remove(1);
  assert.deepEqual(lastCall(app, 'removePoint'), ['removePoint', 0, 1]);
  assert.deepEqual(app.lines[0].points, [{ x: 0, y: 0 }, { x: 10, y: 10 }]);
});

test('line.join appends the other line\'s points and drops the other line', () => {
  const app = makeApp({ lines: [
    { points: [{ x: 0, y: 0 }] },
    { points: [{ x: 9, y: 9 }, { x: 8, y: 8 }] },
  ] });
  const stencil = createStencil(app);

  const lines = stencil.lines;
  lines[0].join(lines[1]);
  assert.equal(app.lines.length, 1);
  assert.deepEqual(app.lines[0].points, [{ x: 0, y: 0 }, { x: 9, y: 9 }, { x: 8, y: 8 }]);
  assert.deepEqual(lastCall(app, 'removeLine'), ['removeLine', 1]);
});

// ── Points ──────────────────────────────────────────────────────────────────────
test('points getter targets the current line; point.apply/move route through setPointCoord', () => {
  const app = makeApp({ currentLine: { points: [{ x: 100, y: 100 }] } });
  const stencil = createStencil(app);

  const pt = stencil.points[0];
  assert.equal(pt.x, 100);
  assert.equal(pt.y, 100);

  pt.apply({ x: 5, y: 6 });
  assert.deepEqual(app.currentLine.points[0], { x: 5, y: 6 });

  pt.move({ x: 10 });   // relative: 5 + 10
  assert.equal(app.currentLine.points[0].x, 15);
});


// ── Line individual setters + point setters/remove ──────────────────────────────────
test('individual line setters commit each change', () => {
  const app = makeApp({ lines: [{ color: '#000000', thickness: 1, pointSize: 1, style: 'solid', fillColor: 'transparent', points: [{ x: 0, y: 0 }] }] });
  const stencil = createStencil(app);
  const line = stencil.lines[0];

  line.color = '#ABC';      assert.equal(app.lines[0].color, '#aabbcc');
  line.thickness = 4;       assert.equal(app.lines[0].thickness, 4);
  line.pointSize = 8;      assert.equal(app.lines[0].pointSize, 8);
  line.style = 'dotted';    assert.equal(app.lines[0].style, 'dotted');
  line.fillColor = '#3399ff'; assert.equal(app.lines[0].fillColor, '#3399ff');
  line.fillColor = null;    assert.equal(app.lines[0].fillColor, 'transparent');   // null → transparent
  assert.equal(called(app, 'saveHistory').length, 6);   // one commit per setter
});

test('point x/y setters write absolute coords; pt.remove drops the point (and empties → drops the line)', () => {
  const app = makeApp({ lines: [{ points: [{ x: 1, y: 2 }] }] });
  const stencil = createStencil(app);

  const pt = stencil.lines[0].points[0];
  pt.x = 50; pt.y = 60;
  assert.deepEqual(app.lines[0].points[0], { x: 50, y: 60 });
  assert.deepEqual(lastCall(app, 'setPointCoord'), ['setPointCoord', 0, 0, 'y', 60]);

  // Removing the only point empties the line → the line is dropped, and remove() returns the facade.
  assert.equal(pt.remove(), stencil);
  assert.equal(app.lines.length, 0);
});
