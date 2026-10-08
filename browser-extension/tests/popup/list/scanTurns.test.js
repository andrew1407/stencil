// popup/list/scan.js's turn token: two scans racing over gated tab reads, the older one
// answering last, and only the newer one's list surviving.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createScanTurns } from '../../../src/popup/list/scanTurns.js';

const gate = () => { let open; const p = new Promise((r) => { open = r; }); return { p, open }; };

test('an older scan that answers last never overwrites the newer list', async () => {
  const turns = createScanTurns();
  const state = { all: [] };
  const scan = async (read, rows) => {
    const isCurrent = turns.begin();
    state.all = [];
    await read;
    if (!isCurrent()) return 'stale';
    state.all = rows;
    return 'drawn';
  };
  const tabA = gate(), tabB = gate();
  const first = scan(tabA.p, ['a1', 'a2']);
  const second = scan(tabB.p, ['b1']);
  tabB.open();
  assert.equal(await second, 'drawn');
  tabA.open();
  assert.equal(await first, 'stale');
  assert.deepEqual(state.all, ['b1']);
});

test('a lone scan stays current; each begin retires the one before', () => {
  const turns = createScanTurns();
  const a = turns.begin();
  assert.equal(a(), true);
  const b = turns.begin();
  assert.equal(a(), false);
  assert.equal(b(), true);
});
