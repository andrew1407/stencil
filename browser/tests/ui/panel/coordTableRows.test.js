// The points table during a drag: the rows it already lists are rewritten in place, never
// rebuilt, and every row interaction rides one delegated listener on the body.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';

let created = 0;
const makeRow = () => {
  const cells = Array.from({ length: 6 }, () => createStubElement('td'));
  const row = createStubElement('tr', {
    querySelector: (sel) => (sel === '.cell-px-x' ? cells[1] : sel === '.cell-px-y' ? cells[2] : null),
    querySelectorAll: () => cells,
  });
  row.closest = () => row;
  row.cells = cells;
  return row;
};
globalThis.document = { createElement: () => { created++; return makeRow(); } };
const { CoordTable } = await import('../../../js/ui/panel/coordTable.js');

const rig = () => {
  const body = createStubElement('tbody', {
    rows: [],
    appendChild(r) { body.rows.push(r); },
    querySelectorAll: () => body.rows,
    querySelector: (sel) => body.rows[Number(/"(\d+)"/.exec(sel)[1])] ?? null,
  });
  Object.defineProperty(body, 'innerHTML', { set() { body.rows = []; }, get: () => '' });
  const app = {
    coordLineIdx: 0, focusedPtIdx: -1, hoveredPtIdx: -1, unit: 'cm', coordinatesBody: body,
    currentLine: null, redraws: 0, renderer: { redraw() { app.redraws++; } },
    compareReadOnly: () => false,
    // A 10 cm page over 100 px: the real pixelToPageCoords reads one cm per 10 px.
    pageSize: 'custom', customPageWidth: 10, customPageHeight: 10, canvas: { width: 100, height: 100 },
  };
  const points = [{ x: 1, y: 2 }, { x: 3, y: 4 }, { x: 5, y: 6 }];
  app.lines = [{ points }];
  return { app, body, points, table: new CoordTable(app) };
};

test('refreshRows rewrites the listed rows in place instead of rebuilding them', () => {
  const { body, points, table } = rig();
  table.update(points, 0);
  const rows = body.rows.slice();
  created = 0;
  for (const p of points) { p.x += 100; p.y += 50; }
  table.refreshRows(points, 0);
  assert.equal(created, 0, 'no row is created during a drag move');
  assert.deepEqual(body.rows, rows, 'the same rows stay in the table');
  assert.equal(rows[2].cells[1].textContent, 105);
  assert.equal(rows[2].cells[3].textContent, '10.50');
});

test('refreshRows falls back to a rebuild when the table lists something else', () => {
  const { body, points, table } = rig();
  table.update(points, 0);
  created = 0;
  table.refreshRows(points.concat({ x: 9, y: 9 }), 0);
  assert.equal(created, 4);
  assert.equal(body.rows.length, 4);
});

test('one delegated listener per event on the body, none on the rows, however often it rebuilds', () => {
  const { app, body, points, table } = rig();
  for (let i = 0; i < 3; i++) table.update(points, 0);
  for (const type of ['mouseover', 'mouseout', 'click', 'keydown', 'dblclick'])
    assert.equal(body.listeners[type]?.length, 1, type);
  assert.ok(body.rows.every((r) => Object.keys(r.listeners).length === 0));
  body.dispatch('mouseover', { target: body.rows[1], relatedTarget: null });
  assert.equal(app.hoveredPtIdx, 1, 'entering a row hovers its point');
  body.dispatch('mouseout', { target: body.rows[1], relatedTarget: body.rows[1] });
  assert.equal(app.hoveredPtIdx, 1, 'a move inside the same row is not a leave');
  body.dispatch('mouseout', { target: body.rows[1], relatedTarget: null });
  assert.equal(app.hoveredPtIdx, -1);
});
