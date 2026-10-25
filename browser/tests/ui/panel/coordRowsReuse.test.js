// Re-listing the line the points table already shows (js/ui/panel/coordRows.js): the rows are
// kept and rewritten, rows are added or dropped at the end, and the table that results is the one a
// fresh build of the same points gives — an open cell editor cleared, a row mid-exit rebuilt.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installMenuDom } from '../../helpers/menuDom.js';

const doc = installMenuDom();
const { CoordTable } = await import('../../../js/ui/panel/coordTable.js');

const tableOn = (points, extra = {}) => {
  const body = doc.createElement('tbody');
  doc.body.appendChild(body);
  const app = {
    coordLineIdx: 0, focusedPtIdx: -1, hoveredPtIdx: -1, unit: 'cm', coordinatesBody: body,
    currentLine: null, renderer: { redraw() {} }, compareReadOnly: () => false, lines: [{ points }],
    pageSize: 'custom', customPageWidth: 10, customPageHeight: 10, canvas: { width: 100, height: 100 }, ...extra,
  };
  return { app, body, table: new CoordTable(app) };
};
// What a row shows: its index, classes and every cell's text.
const shown = (body) => body.children.map((r) => ({
  idx: r.dataset.ptIdx, cls: String(r.className).split(/\s+/).filter(Boolean).sort().join(' '), cells: r.children.slice(0, 5).map((c) => c.textContent.trim()),
  editor: !!r.querySelector('.coord-px-input'),
}));
const fresh = (points, extra) => { const t = tableOn(points, extra); t.table.update(points, 0); return shown(t.body); };

test('a re-list keeps the rows and ends exactly as a fresh build of the new points', () => {
  const points = [{ x: 1, y: 2 }, { x: 30, y: 40 }, { x: 55, y: 66 }];
  const t = tableOn(points, { focusedPtIdx: 1 });
  t.table.update(points, 0);
  const first = t.body.children[0];
  points[0].x = 90;
  points.push({ x: 12, y: 13 });
  t.app.focusedPtIdx = 3;
  t.table.update(points, 0);
  assert.equal(t.body.children[0], first, 'the row is kept');
  assert.deepEqual(shown(t.body), fresh(points, { focusedPtIdx: 3 }));
  points.splice(1, 2);
  t.app.focusedPtIdx = -1;
  t.table.update(points, 0);
  assert.deepEqual(shown(t.body), fresh(points), 'rows dropped at the end, the rest renumbered');
  assert.equal(t.body.children.length, 2);
});

test('an open cell editor and a hover tint are gone after a re-list, as after a rebuild', () => {
  const points = [{ x: 1, y: 2 }, { x: 3, y: 4 }];
  const t = tableOn(points);
  t.table.update(points, 0);
  const cell = t.body.children[1].querySelector('.cell-px-x');
  cell.innerHTML = '<input type="number" class="coord-px-input">';
  t.body.children[0].classList.add('row-highlighted');
  t.table.update(points, 0);
  assert.deepEqual(shown(t.body), fresh(points));
});

test('a row mid-exit makes the re-list a full rebuild', () => {
  const points = [{ x: 1, y: 2 }, { x: 3, y: 4 }];
  const t = tableOn(points);
  t.table.update(points, 0);
  const first = t.body.children[0];
  first.classList.add('leaving');
  t.table.update(points, 0);
  assert.notEqual(t.body.children[0], first);
  assert.deepEqual(shown(t.body), fresh(points));
});
