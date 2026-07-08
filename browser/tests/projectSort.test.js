// Unit tests for the pure projects-modal sort/order helpers (js/ui/projectSort.js).
// The modal's DOM wiring isn't node-testable, but these comparators + the manual-order
// reconciliation are pure, so they carry the behavioral contract for the five sort modes
// and drag-reorder.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { SORT_MODES, sortProjectItems, reconcileManualOrder } from '../js/ui/projectSort.js';

// Item factory: key, lowercased name, date (epoch ms), isRemote.
const it = (key, name, date, isRemote) => ({ key, name: name.toLowerCase(), date, isRemote });

// A mixed local/server set with interleaving names + dates.
const sample = () => [
  it('local:1', 'banana', 300, false),
  it('remote:s:9', 'apple', 100, true),
  it('local:2', 'cherry', 200, false),
  it('remote:s:8', 'date', 400, true),
];
const keys = (arr) => arr.map((i) => i.key);

test('SORT_MODES lists the six selectable modes', () => {
  assert.deepEqual(SORT_MODES, ['name', 'local', 'server', 'date-desc', 'date-asc', 'manual']);
});

test('name (default) interleaves server + local alphabetically', () => {
  assert.deepEqual(keys(sortProjectItems(sample(), 'name')),
    ['remote:s:9', 'local:1', 'local:2', 'remote:s:8']);  // apple, banana, cherry, date
});

test('unknown mode falls back to name ordering', () => {
  assert.deepEqual(keys(sortProjectItems(sample(), 'whatever')), keys(sortProjectItems(sample(), 'name')));
});

test('local-first groups locals before servers, each alphabetical', () => {
  assert.deepEqual(keys(sortProjectItems(sample(), 'local')),
    ['local:1', 'local:2', 'remote:s:9', 'remote:s:8']);  // banana,cherry | apple,date
});

test('server-first groups servers before locals', () => {
  assert.deepEqual(keys(sortProjectItems(sample(), 'server')),
    ['remote:s:9', 'remote:s:8', 'local:1', 'local:2']);  // apple,date | banana,cherry
});

test('date-desc / date-asc order by date across local + server', () => {
  assert.deepEqual(keys(sortProjectItems(sample(), 'date-desc')),
    ['remote:s:8', 'local:1', 'local:2', 'remote:s:9']);  // 400,300,200,100
  assert.deepEqual(keys(sortProjectItems(sample(), 'date-asc')),
    ['remote:s:9', 'local:2', 'local:1', 'remote:s:8']);  // 100,200,300,400
});

test('sortProjectItems never mutates its input', () => {
  const src = sample();
  const before = keys(src);
  sortProjectItems(src, 'date-desc');
  assert.deepEqual(keys(src), before);
});

test('manual mode follows the saved order; unknown keys fall to the end by name', () => {
  const order = ['local:2', 'remote:s:8'];  // only two of four keys are placed
  const out = keys(sortProjectItems(sample(), 'manual', order));
  // Placed keys first in order, then the rest by name (apple, banana).
  assert.deepEqual(out, ['local:2', 'remote:s:8', 'remote:s:9', 'local:1']);
});

test('reconcileManualOrder seeds from full order and moves the dragged key after the target', () => {
  const full = ['a', 'b', 'c', 'd'];
  // No existing manual order → seed from full, move a to AFTER c.
  assert.deepEqual(reconcileManualOrder(full, [], 'a', 'c', false), ['b', 'c', 'a', 'd']);
  // before=true → land BEFORE c.
  assert.deepEqual(reconcileManualOrder(full, [], 'a', 'c', true), ['b', 'a', 'c', 'd']);
});

test('reconcileManualOrder keeps an existing manual order and appends newly-seen keys', () => {
  const existing = ['c', 'a'];               // b + d are new since this order was saved
  const full = ['a', 'b', 'c', 'd'];
  const out = reconcileManualOrder(full, existing, 'd', 'c', true);
  // Start c,a → append missing b,d → c,a,b,d → move d before c → d,c,a,b
  assert.deepEqual(out, ['d', 'c', 'a', 'b']);
});

test('reconcileManualOrder drops a missing target to the end', () => {
  const full = ['a', 'b', 'c'];
  // target 'zzz' isn't present → dragged key goes to the end.
  assert.deepEqual(reconcileManualOrder(full, [], 'a', 'zzz', false), ['b', 'c', 'a']);
});
