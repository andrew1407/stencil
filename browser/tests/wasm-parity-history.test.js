// Parity coverage for the line-snapshot history (core/state/HistoryStack.cpp via the handle ABI in
// core/wasmStateApi.cpp) against its JS twin, js/core/historyStack.js. Snapshots cross as the flat
// (nums, text) pair, so two things are pinned at once: the cursor semantics — push truncation,
// canUndo/canRedo, the "step 0 → empty, step -1" undo — and the codec halves (js/core/linesCodec.js ↔
// core/abi/linesCodec.hpp), whose symmetry is proved by pushing a line in and reading it back out.
import { test, before } from 'node:test';
import assert from 'node:assert';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { core } from '../js/core/abi/stencilCore.js';
import { HistoryStack, MAX_STEPS } from '../js/core/historyStack.js';
import { encodeLines } from '../js/core/line/linesCodec.js';

const MODULE_BUILT = existsSync(fileURLToPath(new URL('../js/wasm/stencilCore.js', import.meta.url)));
const wtest = MODULE_BUILT ? test : test.skip;

before(async () => {
  if (!MODULE_BUILT) return;
  const ok = await core.init();
  assert.strictEqual(ok, true, 'wasm core must load in Node (SINGLE_FILE ES module)');
});

// A complete core Line — every field explicit, so the twins are compared on the model,
// not on which keys a caller happened to set (see the sparse-line case below).
const line = (over = {}) => ({
  points: [{ x: 1, y: 2 }, { x: 3.5, y: -4.25 }],
  color: '#7c3aed', thickness: 2, pointSize: 4, style: 'solid',
  locked: false, fillColor: 'transparent', pointColor: '',
  ...over,
});

// A relabeling applied to BOTH halves of the JS codec would cancel out inside the round trip, so the layout
// itself is pinned: this is the byte contract core/abi/linesCodec.hpp reads. Runs without wasm.
test('linesCodec: the encoded layout is exactly what the C++ decoder expects', () => {
  const { nums, text } = encodeLines([{
    points: [{ x: 1, y: 2 }], color: '#ab', thickness: 3, pointSize: 4,
    style: 'dd', locked: true, fillColor: 'ccc', pointColor: '',
  }]);
  assert.deepStrictEqual(Array.from(nums), [1, 1, 3, 4, 1, 3, 2, 3, 0, 1, 2]);
  assert.strictEqual(new TextDecoder().decode(text), '#abddccc');
});

const OPS = {
  reset: (h, [lines, step]) => { step === undefined ? h.reset(lines) : h.reset(lines, step); return null; },
  push: (h, [lines]) => { h.push(lines); return null; },
  undo: (h) => h.undo(),
  redo: (h) => h.redo(),
};

// Run `script` through both stacks, asserting every returned snapshot and every
// observable field match after each step.
const drive = (script) => {
  const js = new HistoryStack();
  const wasm = new (core.op('HistoryStack'))();
  const log = [];
  try {
    script.forEach(([op, ...args], i) => {
      const where = `step ${i} (${op})`;
      const jsOut = OPS[op](js, args);
      assert.deepStrictEqual(OPS[op](wasm, args), jsOut, `${where}: snapshot`);
      assert.strictEqual(wasm.historyStep, js.historyStep, `${where}: historyStep`);
      assert.strictEqual(wasm.size, js.history.length, `${where}: size`);
      assert.strictEqual(wasm.canUndo(), js.canUndo(), `${where}: canUndo`);
      assert.strictEqual(wasm.canRedo(), js.canRedo(), `${where}: canRedo`);
      log.push(jsOut);
    });
  } finally {
    wasm.destroy();
  }
  return log;
};

wtest('history: the wasm class is installed alongside the pure ops', () => {
  assert.strictEqual(typeof core.op('HistoryStack'), 'function');
  assert.ok(core.opNames.includes('HistoryStack'));
});

wtest('history: a pushed line comes back field for field (codec round trip)', () => {
  const a = [line({ color: '#abcdef', style: 'dashed', locked: true, fillColor: '', pointColor: '#0f0', thickness: 3.5, pointSize: 7.25 })];
  const b = [line(), line({ points: [], color: '#000000' })];
  const log = drive([['push', a], ['push', b], ['undo'], ['redo']]);
  assert.deepStrictEqual(log[2], a);
  assert.deepStrictEqual(log[3], b);
});

wtest('history: undo at step 0 yields an empty snapshot and step -1', () => {
  const log = drive([
    ['push', [line()]],
    ['undo'],                       // step 0 → [] and step -1, the pinned quirk
    ['undo'],                       // nothing left → null
    ['redo'],
  ]);
  assert.deepStrictEqual(log[1], []);
  assert.deepStrictEqual(log[2], null);
  assert.deepStrictEqual(log[3], [line()]);
});

wtest('history: a push after an undo drops the redo branch', () => {
  const first = [line({ color: '#111111' })];
  const second = [line({ color: '#222222' })];
  const third = [line({ color: '#333333' })];
  const log = drive([
    ['push', first], ['push', second], ['push', third],
    ['undo'], ['undo'],             // back to `first`
    ['push', [line({ color: '#444444' })]],
    ['redo'],                       // the branch holding second/third is gone
  ]);
  assert.deepStrictEqual(log[3], second);
  assert.deepStrictEqual(log[4], first);
  assert.deepStrictEqual(log[6], null);
});

wtest('history: reset takes the default base step, an explicit one, and an empty load', () => {
  drive([
    ['reset', [line()]],            // default: lines present → step 0
    ['undo'],
    ['reset', []],                  // default: no lines → step -1, empty history
    ['redo'],
    ['reset', [line()], -1],        // explicit -1: keep the history empty
    ['redo'],
    ['reset', [line(), line()], 0],
    ['undo'],
    ['reset', [], 0],               // an empty snapshot is still a snapshot at step 0
    ['undo'],
  ]);
});

wtest('history: unicode, empty and long strings survive the text buffer', () => {
  const exotic = [line({ color: '', style: 'ρωμαϊκό — 日本語 🎨', fillColor: 'x'.repeat(5000), pointColor: '#ábç' })];
  const log = drive([['push', exotic], ['push', [line()]], ['undo']]);
  assert.deepStrictEqual(log[2], exotic);
});

wtest('history: snapshots are deep copies — mutating the live lines never edits history', () => {
  const live = [line()];
  const log = drive([['push', live], ['push', [line({ color: '#999999' })]]]);
  live[0].points[0].x = 999;
  live[0].color = '#000000';
  const js = new HistoryStack();
  const wasm = new (core.op('HistoryStack'))();
  try {
    const fresh = [line()];
    js.push(fresh); wasm.push(fresh);
    fresh[0].points[0].x = 999;
    assert.deepStrictEqual(wasm.undo(), js.undo());
    assert.strictEqual(log.length, 2);
  } finally {
    wasm.destroy();
  }
});

// A line missing fields is completed from the core's defaults (core/models.hpp) on the way through the ABI —
// the one place the wasm twin is not byte-identical to the JS one.
wtest('history: a sparse line decodes complete, from the core defaults', () => {
  const wasm = new (core.op('HistoryStack'))();
  try {
    wasm.push([{ points: [{ x: 0, y: 0 }] }]);
    wasm.push([{ points: [] }]);
    assert.deepStrictEqual(wasm.undo(), [{
      points: [{ x: 0, y: 0 }], color: '#FFFF00', thickness: 2, pointSize: 4,
      style: 'solid', locked: false, fillColor: 'transparent', pointColor: '',
    }]);
  } finally {
    wasm.destroy();
  }
});

// A long random op stream: the op-for-op proof the hand-kept twins never had.
wtest('history: 600 random push/undo/redo/reset steps stay identical step for step', () => {
  let seed = 20260912;
  const rnd = (n) => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };
  const snap = () => Array.from({ length: rnd(3) }, (_, k) => line({
    color: `#${(rnd(4096) + 4096).toString(16)}`,
    points: Array.from({ length: rnd(4) }, (__, p) => ({ x: p + k, y: rnd(50) })),
    locked: rnd(2) === 0,
    pointSize: rnd(10),
  }));
  const script = [];
  for (let i = 0; i < 600; i++) {
    const pick = rnd(10);
    if (pick < 4) script.push(['push', snap()]);
    else if (pick < 7) script.push(['undo']);
    else if (pick < 9) script.push(['redo']);
    else script.push(['reset', snap(), rnd(3) === 0 ? undefined : rnd(2) - 1]);
  }
  const log = drive(script);
  assert.ok(log.filter((x) => Array.isArray(x) && x.length).length > 50, 'expected many real snapshots');
});

// The cap is the one place a push can move snapshots the cursor is not on, so drive it
// past the edge both ways: straight through, and with a redo branch to truncate first.
wtest('history: the depth cap evicts the same snapshot on both sides', () => {
  const script = [];
  for (let i = 0; i < MAX_STEPS + 5; i++) script.push(['push', [line({ pointSize: i })]]);
  for (let i = 0; i < 10; i++) script.push(['undo']);
  for (let i = 0; i < MAX_STEPS; i++) script.push(['push', [line({ thickness: i })]]);
  for (let i = 0; i < MAX_STEPS + 5; i++) script.push(['undo']);
  const log = drive(script);
  assert.ok(log.some((x) => Array.isArray(x) && x.length === 0), 'expected the step -1 stop');
});

wtest('history: handles are independent and destroy() releases one for good', () => {
  const Wasm = core.op('HistoryStack');
  const a = new Wasm();
  const b = new Wasm();
  try {
    a.push([line()]);
    assert.strictEqual(b.historyStep, -1);
    assert.strictEqual(b.undo(), null);
    assert.strictEqual(a.historyStep, 0);
  } finally {
    a.destroy();
    b.destroy();
  }
  a.destroy();                                   // idempotent
  assert.throws(() => a.historyStep, /destroyed/);
});
