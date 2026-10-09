// Parity for the history's points budget (core/state/HistoryStack.cpp MAX_POINTS ↔ js/core/historyStack.js):
// pushes of large snapshots evict the same oldest steps, and undo walks back through the same ones.
import { test, before } from 'node:test';
import assert from 'node:assert';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { core } from '../../js/core/abi/stencilCore.js';
import { HistoryStack, MAX_POINTS } from '../../js/core/historyStack.js';

const MODULE_BUILT = existsSync(fileURLToPath(new URL('../../js/wasm/stencilCore.js', import.meta.url)));
const wtest = MODULE_BUILT ? test : test.skip;

before(async () => {
  if (!MODULE_BUILT) return;
  assert.strictEqual(await core.init(), true);
});

// Lines of 50k points: the codec caps a line at LIMITS.layoutLinePointsMax and a layout at layoutPointsMax.
const lines = (x, n) => Array.from({ length: Math.ceil(n / 50_000) }, (_, i) => ({
  points: Array.from({ length: Math.min(50_000, n - i * 50_000) }, () => ({ x, y: 0 })), color: '#7c3aed',
  thickness: 2, pointSize: 4, style: 'solid', locked: false, fillColor: 'transparent', pointColor: '', name: '', hidden: false,
}));

wtest('history: the points budget evicts the same steps in both twins', () => {
  const js = new HistoryStack();
  const wasm = new (core.op('HistoryStack'))();
  try {
    const sizes = [0.3, 0.3, 0.3, 0.5, 0.1, 0.45, 0.2].map((f) => Math.ceil(MAX_POINTS * f));
    sizes.forEach((n, i) => {
      js.push(lines(i, n));
      wasm.push(lines(i, n));
      assert.strictEqual(wasm.size, js.history.length, `push ${i}: size`);
      assert.strictEqual(wasm.historyStep, js.historyStep, `push ${i}: historyStep`);
    });
    for (let i = 0; i <= sizes.length; i++) {
      const a = js.undo();
      const b = wasm.undo();
      const shape = (snap) => snap?.map((l) => [l.points.length, l.points[0]?.x]);
      assert.deepStrictEqual(shape(b), shape(a), `undo ${i}`);
    }
  } finally {
    wasm.destroy();
  }
});
