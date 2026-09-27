// Bare Delete/Backspace on a focused coordinates row (js/ui/panel/coordTable.js), the parity port of the desktop
// points table, whose SelectionPanel::eventFilter takes the same key. Pinned: both keys remove that point
// through the same removePoint(app, lineIdx, ptIdx) the row's 🗑 uses (core/line/editOps.js, run for real);
// the key is swallowed so it never also reaches the global hotkey dispatcher; it is INERT inside a px cell's
// inline <input>, where Backspace erases a digit; and inert in the read-only compare view. update()'s few DOM
// calls are stubbed, and so is the app's own coord-table collaborator, so the table under test is not rebuilt.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';

// ── Minimal DOM: enough for CoordTable.update() to build its rows ───────────────
// Each created <tr> is a shared stub whose querySelector hands back inert cell stubs.
const makeStubEl = () => createStubElement('td');

let createdRows = [];

globalThis.document = {
  createElement: () => {
    const row = createStubElement('tr', { tabIndex: -1, querySelector: () => makeStubEl() });
    row.closest = () => row;
    createdRows.push(row);
    return row;
  },
};

const { CoordTable } = await import('../../../js/ui/panel/coordTable.js');
const { Emitter } = await import('../../../js/core/emitter.js');

const POINTS = [{ x: 10, y: 10 }, { x: 20, y: 20 }, { x: 30, y: 30 }];

// A stub editor exposing what update(), the handler and the real removePoint read; line 2 is the
// coord line, a copy of POINTS. `history` counts the undo steps, `signals` the change topics.
const makeApp = ({ readOnly = false } = {}) => {
  const body = createStubElement('tbody', {
    rows: [],
    appendChild(r) { body.rows.push(r); },
    querySelectorAll: () => body.rows,
  });
  const signals = [];
  const changes = new Emitter();
  for (const t of ['lines', 'selection']) changes.on(t, () => signals.push(t));
  const app = {
    history: [],
    signals,
    changes,
    lines: [{ points: [] }, { points: [] }, { points: POINTS.map((p) => ({ ...p })) }],
    currentLine: null,
    selectedLineIdx: -1,
    coordLineIdx: 2,
    focusedPtIdx: -1,
    hoveredPtIdx: -1,
    unit: 'cm',
    pageSize: 'A4',
    canvas: { width: 100, height: 100 },
    coordinatesBody: body,
    renderer: { redraw() {} },
    coordTable: { update() {} },
    compareReadOnly: () => readOnly,
    saveHistory() { app.history.push(app.lines[2].points.map((p) => ({ ...p }))); },
  };
  return app;
};

// The coord line's points after the key: what the real removePoint left behind.
const left = (app) => app.lines[2].points;

// Build the table and return a keydown fired from row `i` (the body delegates it), plus the app.
const rowKeydown = (i, opts) => {
  createdRows = [];
  const app = makeApp(opts);
  new CoordTable(app).update(POINTS, 2);
  const row = createdRows[i];
  const fire = (event) => app.coordinatesBody.listeners.keydown[0](
    { ...event, target: event.target ? { ...event.target, closest: () => row } : row });
  return { app, fire, rows: createdRows };
};

const keyEvent = (key, target = null) => {
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
    assert.deepEqual(left(app), [POINTS[0], POINTS[2]], 'removes the row index off the coord line');
    assert.equal(app.history.length, 1, 'one undo step');
    assert.deepEqual(app.signals, ['lines', 'selection']);
    assert.equal(calls.prevented, 1, 'must preventDefault');
    assert.equal(calls.stopped, 1, 'must not also reach the global hotkey dispatcher');
  });
}

test('other keys are ignored', () => {
  const { app, fire } = rowKeydown(1);
  for (const key of ['a', 'Enter', 'ArrowDown', 'Escape']) fire(keyEvent(key).ev);
  assert.deepEqual(left(app), POINTS);
  assert.equal(app.history.length, 0);
});

test('inert while a px cell is being edited (Backspace erases a digit there)', () => {
  const { app, fire } = rowKeydown(1);
  const { ev, calls } = keyEvent('Backspace', { tagName: 'INPUT', type: 'number' });
  fire(ev);
  assert.deepEqual(left(app), POINTS, 'the inline editor owns the key');
  assert.equal(calls.prevented, 0, 'and the event is left alone for the input');
});

test('inert in the read-only compare view', () => {
  const { app, fire } = rowKeydown(1, { readOnly: true });
  fire(keyEvent('Delete').ev);
  assert.deepEqual(left(app), POINTS);
  assert.equal(app.history.length, 0);
});

test('after a delete, focus lands on the row that took its place', () => {
  const { fire, rows } = rowKeydown(1);
  fire(keyEvent('Delete').ev);
  // The app's coord table is stubbed, so this one is not rebuilt: focus goes to the same index.
  assert.ok(rows[1].focused, 'a run of deletes must keep working without re-clicking');
});

test('deleting the last row falls back to the new last row', () => {
  const { app, fire, rows } = rowKeydown(2);
  // Shrink the table the way a real removePoint→update() would before re-focusing.
  app.coordinatesBody.rows = rows.slice(0, 2);
  fire(keyEvent('Delete').ev);
  assert.deepEqual(left(app), [POINTS[0], POINTS[1]]);
  assert.ok(rows[1].focused, 'clamps to the last surviving row');
});
