// Bare Delete/Backspace on a focused coordinates row (js/ui/coordTable.js), the parity port of the desktop
// points table, whose SelectionPanel::eventFilter takes the same key. Pinned: both keys remove that point
// through the same app.removePoint(lineIdx, ptIdx) the row's 🗑 uses; the key is swallowed so it never also
// reaches the global hotkey dispatcher; it is INERT inside a px cell's inline <input>, where Backspace erases
// a digit; and inert in the read-only compare view. update()'s few DOM calls are stubbed.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from './helpers/dom.js';

// ── Minimal DOM: enough for CoordTable.update() to build its rows ───────────────
// Each created <tr> is a shared stub whose querySelector hands back inert cell stubs.
const makeStubEl = () => createStubElement('td');

let createdRows = [];

globalThis.document = {
  createElement: () => {
    const row = createStubElement('tr', { tabIndex: -1, querySelector: () => makeStubEl() });
    createdRows.push(row);
    return row;
  },
};

const { CoordTable } = await import('../js/ui/coordTable.js');

// A stub editor exposing only what update() + the handler read.
const makeApp = ({ readOnly = false } = {}) => {
  const removed = [];
  const body = {
    innerHTML: '',
    rows: [],
    appendChild(r) { this.rows.push(r); },
    querySelectorAll: () => body.rows,
  };
  return {
    removed,
    coordLineIdx: 2,
    focusedPtIdx: -1,
    hoveredPtIdx: -1,
    unit: 'cm',
    coordinatesBody: body,
    renderer: { redraw() {} },
    pixelToPageCoords: () => ({ x: 0, y: 0 }),
    compareReadOnly: () => readOnly,
    removePoint(lineIdx, ptIdx) { removed.push([lineIdx, ptIdx]); },
  };
};

const POINTS = [{ x: 10, y: 10 }, { x: 20, y: 20 }, { x: 30, y: 30 }];

// Build the table and return the keydown handler of row `i`, plus the app.
const rowKeydown = (i, opts) => {
  createdRows = [];
  const app = makeApp(opts);
  new CoordTable(app).update(POINTS, 2);
  return { app, fire: (event) => createdRows[i].listeners.keydown[0](event), rows: createdRows };
};

const keyEvent = (key, target = { tagName: 'TR' }) => {
  const calls = { prevented: 0, stopped: 0 };
  return {
    ev: { key, target, preventDefault: () => calls.prevented++, stopPropagation: () => calls.stopped++ },
    calls,
  };
};

test('rows are focusable, so the key can be scoped to this table', () => {
  const { rows } = rowKeydown(0);
  assert.equal(rows.length, POINTS.length);
  for (const r of rows) assert.equal(r.tabIndex, 0);
});

for (const key of ['Delete', 'Backspace']) {
  test(`${key} on a focused row removes that point via removePoint`, () => {
    const { app, fire } = rowKeydown(1);
    const { ev, calls } = keyEvent(key);
    fire(ev);
    assert.deepEqual(app.removed, [[2, 1]], 'removes the row index off the coord line');
    assert.equal(calls.prevented, 1, 'must preventDefault');
    assert.equal(calls.stopped, 1, 'must not also reach the global hotkey dispatcher');
  });
}

test('other keys are ignored', () => {
  const { app, fire } = rowKeydown(1);
  for (const key of ['a', 'Enter', 'ArrowDown', 'Escape']) fire(keyEvent(key).ev);
  assert.deepEqual(app.removed, []);
});

test('inert while a px cell is being edited (Backspace erases a digit there)', () => {
  const { app, fire } = rowKeydown(1);
  const { ev, calls } = keyEvent('Backspace', { tagName: 'INPUT', type: 'number' });
  fire(ev);
  assert.deepEqual(app.removed, [], 'the inline editor owns the key');
  assert.equal(calls.prevented, 0, 'and the event is left alone for the input');
});

test('inert in the read-only compare view', () => {
  const { app, fire } = rowKeydown(1, { readOnly: true });
  fire(keyEvent('Delete').ev);
  assert.deepEqual(app.removed, []);
});

test('after a delete, focus lands on the row that took its place', () => {
  const { fire, rows } = rowKeydown(1);
  fire(keyEvent('Delete').ev);
  // removePoint is stubbed, so the table is not rebuilt: focus goes to the same index.
  assert.ok(rows[1].focused, 'a run of deletes must keep working without re-clicking');
});

test('deleting the last row falls back to the new last row', () => {
  const { app, fire, rows } = rowKeydown(2);
  // Shrink the table the way a real removePoint→update() would before re-focusing.
  app.coordinatesBody.rows = rows.slice(0, 2);
  fire(keyEvent('Delete').ev);
  assert.deepEqual(app.removed, [[2, 2]]);
  assert.ok(rows[1].focused, 'clamps to the last surviving row');
});
