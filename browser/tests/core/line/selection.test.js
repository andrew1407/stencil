// The per-frame selection test the renderer and the Lines tab ask once per line, in both
// modes: single-select reads selectedLineIdx, a multi-selection (even of one) is the set and
// ignores it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { selectionPredicate } from '../../../js/core/line/selection.js';

const cases = [
  { app: { selectedLines: [], selectedLineIdx: -1 }, selected: [] },
  { app: { selectedLines: [], selectedLineIdx: 2 }, selected: [2] },
  { app: { selectedLines: [0, 3, 5], selectedLineIdx: -1 }, selected: [0, 3, 5] },
  { app: { selectedLines: [4], selectedLineIdx: 1 }, selected: [4] },
];

test('selectionPredicate answers every line in both modes', () => {
  for (const { app, selected } of cases) {
    const isSelected = selectionPredicate(app);
    for (let i = 0; i < 8; i++) assert.equal(isSelected(i), selected.includes(i), `${JSON.stringify(app)} @ ${i}`);
  }
});

test('the predicate is a snapshot: a later edit to the selection needs a new one', () => {
  const app = { selectedLines: [1, 2], selectedLineIdx: -1 };
  const selected = selectionPredicate(app);
  app.selectedLines.push(3);
  assert.equal(selected(3), false);
  assert.equal(selectionPredicate(app)(3), true);
});
