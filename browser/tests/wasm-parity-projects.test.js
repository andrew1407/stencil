// Parity coverage for the project rules shared with core/state/ProjectsStore.cpp — the refresh presets and the
// expiry predicates — reached through the scalar ABI in core/wasmProjectsApi.cpp. The registry itself is NOT
// pinned: the browser store is localStorage-backed and the core one in-memory, so what must agree is the
// arithmetic, driven from the same inputs against the JS store's own methods.
import { test, before } from 'node:test';
import assert from 'node:assert';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { core } from '../js/core/stencilCore.js';
import {
  ProjectsStore, periodMs, addPeriod, shouldPersist, PERIOD_ORDER, EXPIRY_MS, WARN_MS,
} from '../js/core/projectsStore.js';
import { createMemoryStorage } from './helpers/memoryStorage.js';

const MODULE_BUILT = existsSync(fileURLToPath(new URL('../js/wasm/stencilCore.js', import.meta.url)));
const wtest = MODULE_BUILT ? test : test.skip;

// The JS reference: the store's own predicates, over a storage it never has to touch.
const js = new ProjectsStore(createMemoryStorage({}));
const NOW = 1757000000000;   // 2025-09, well past 2^32 — epoch ms must cross exactly

before(async () => {
  if (!MODULE_BUILT) return;
  const ok = await core.init();
  assert.strictEqual(ok, true, 'wasm core must load in Node (SINGLE_FILE ES module)');
});

wtest('projects: every preset (and every unknown spelling) agrees on its duration', () => {
  const fn = core.op('projectPeriodMs');
  for (const period of [...PERIOD_ORDER, '', 'banana', 'Week', '2month', null, undefined]) {
    assert.strictEqual(fn(period), periodMs(period), `periodMs ${period}`);
  }
  assert.strictEqual(fn('week'), EXPIRY_MS);
});

wtest('projects: addPeriod lands on the same epoch millisecond', () => {
  const fn = core.op('projectAddPeriod');
  for (const from of [0, 1, NOW, NOW + 86399999, 8.9e15]) {
    for (const period of [...PERIOD_ORDER, 'nonsense', '']) {
      assert.strictEqual(fn(from, period), addPeriod(from, period), `addPeriod ${from} ${period}`);
    }
  }
});

wtest('projects: shouldPersist agrees on all four combinations', () => {
  const fn = core.op('projectShouldPersist');
  for (const activeId of ['p_1', '', null, undefined]) {
    for (const temporary of [true, false]) {
      assert.strictEqual(fn(activeId, temporary), shouldPersist(activeId, temporary), `${activeId} ${temporary}`);
    }
  }
});

wtest('projects: isExpired and isExpiringSoon agree across the whole window', () => {
  const expired = core.op('projectIsExpired');
  const soon = core.op('projectIsExpiringSoon');
  const offsets = [-EXPIRY_MS, -1, 0, 1, WARN_MS - 1, WARN_MS, WARN_MS + 1, EXPIRY_MS];
  const metas = [null, {}, { expiresAt: 0 }, { expiresAt: null }, ...offsets.map((d) => ({ expiresAt: NOW + d }))];
  for (const meta of metas) {
    assert.strictEqual(expired(meta, NOW), js.isExpired(meta, NOW), `isExpired ${JSON.stringify(meta)}`);
    assert.strictEqual(soon(meta, NOW), js.isExpiringSoon(meta, NOW), `isExpiringSoon ${JSON.stringify(meta)}`);
  }
});

// A project created now, refreshed on its own preset, must reach the same expiry and
// warn at the same moment on both sides — the sequence the Refresh button walks.
wtest('projects: a create → refresh → warn → expire timeline matches step for step', () => {
  const add = core.op('projectAddPeriod');
  const expired = core.op('projectIsExpired');
  const soon = core.op('projectIsExpiringSoon');
  for (const period of PERIOD_ORDER) {
    const at = add(NOW, period);
    assert.strictEqual(at, addPeriod(NOW, period));
    const meta = { expiresAt: at };
    for (const t of [NOW, at - WARN_MS - 1, at - WARN_MS, at - 1, at, at + 1]) {
      assert.strictEqual(expired(meta, t), js.isExpired(meta, t), `${period} expired at ${t}`);
      assert.strictEqual(soon(meta, t), js.isExpiringSoon(meta, t), `${period} soon at ${t}`);
    }
  }
});
