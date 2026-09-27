// Parity for the editor-memento history (core/state/HistoryStack's EditorHistory behind
// core/wasmHistoryApi.cpp) against js/core/historyStack.js: a step's crop, turn and filter cross
// beside its lines, the floor keeps the view the stack started on, and Lines steps still mix in.
import { test, before } from 'node:test';
import assert from 'node:assert';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { core } from '../../js/core/abi/stencilCore.js';
import { HistoryStack } from '../../js/core/historyStack.js';

const MODULE_BUILT = existsSync(fileURLToPath(new URL('../../js/wasm/stencilCore.js', import.meta.url)));
const wtest = MODULE_BUILT ? test : test.skip;

before(async () => {
  if (!MODULE_BUILT) return;
  const ok = await core.init();
  assert.strictEqual(ok, true, 'wasm core must load in Node (SINGLE_FILE ES module)');
});

// Complete core lines, so the codec's defaults never separate the twins.
const line = (x) => ({ points: [{ x, y: 2 }, { x: 3.5, y: -4.25 }], color: '#7c3aed', thickness: 2,
  pointSize: 4, style: 'solid', locked: false, fillColor: 'transparent', pointColor: '' });
const memento = (cropX, rotationQuarters, n, extra = {}) => ({
  lines: Array.from({ length: n }, (_, i) => line(i)),
  cropRect: cropX == null ? null : { x: cropX, y: 1.5, width: 50, height: 40 },
  rotationQuarters,
  ...extra,
});

const OPS = {
  reset: (h, [s, step]) => { step === undefined ? h.reset(s) : h.reset(s, step); return null; },
  push: (h, [s]) => { h.push(s); return null; },
  undo: (h) => h.undo(),
  redo: (h) => h.redo(),
};

const drive = (script) => {
  const js = new HistoryStack();
  const wasm = new (core.op('HistoryStack'))();
  try {
    script.forEach(([op, ...args], i) => {
      const where = `step ${i} (${op})`;
      const jsOut = OPS[op](js, args);
      assert.deepStrictEqual(OPS[op](wasm, args), jsOut, `${where}: snapshot`);
      assert.strictEqual(wasm.historyStep, js.historyStep, `${where}: historyStep`);
      assert.strictEqual(wasm.size, js.history.length, `${where}: size`);
      assert.strictEqual(wasm.canUndo(), js.canUndo(), `${where}: canUndo`);
      assert.strictEqual(wasm.canRedo(), js.canRedo(), `${where}: canRedo`);
    });
  } finally {
    wasm.destroy();
  }
};

wtest('memento history: crops and turns step back and forth, the floor keeps its view', () => {
  drive([
    ['reset', memento(0, 0, 0)],
    ['push', memento(0, 0, 1)],
    ['push', memento(7, 1, 1)],
    ['push', memento(7, 1, 2, { filter: 'custom', filterColor: '#7c3aed' })],
    ['undo'], ['undo'], ['undo'], ['undo'], ['undo'],
    ['redo'], ['redo'], ['redo'], ['redo'],
    ['reset', memento(3, 2, 2), 0],
    ['undo'], ['redo'],
    ['reset', memento(null, 0, 0), -1],
    ['push', memento(null, 0, 1)],
    ['undo'],
  ]);
});

// The app's steps always carry a filter and its tint (js/core/historyStack.js editorMemento).
const tinted = (cropX, n, filter, filterColor) => memento(cropX, 0, n, { filter, filterColor });

wtest('memento history: a filter switch and a tint step back and forth, the floor keeps the loaded filter', () => {
  drive([
    ['reset', tinted(0, 0, 'sepia', '#7c3aed')],
    ['push', tinted(0, 1, 'sepia', '#7c3aed')],
    ['push', tinted(0, 1, 'custom', '#7c3aed')],
    ['push', tinted(0, 1, 'custom', '#00ff00')],
    ['push', tinted(4, 2, 'bw', '#00ff00')],
    ['undo'], ['undo'], ['undo'], ['undo'], ['undo'],
    ['redo'], ['redo'],
    ['push', tinted(0, 1, 'invert', '#123456')],
    ['redo'], ['undo'], ['undo'],
    ['reset', tinted(2, 3, 'contour', '#abcdef'), 0],
    ['undo'], ['undo'], ['redo'],
  ]);
});

wtest('memento history: a step without a filter names none, and trimming keeps the dropped filter', () => {
  const script = [['reset', tinted(0, 0, 'bw', '#7c3aed')], ['push', memento(0, 0, 1)], ['push', [line(3)]]];
  const modes = ['none', 'bw', 'sepia', 'custom'];
  for (let i = 0; i < 70; i++) script.push(['push', tinted(i, 1, modes[i % 4], `#0000${String(i).padStart(2, '0')}`)]);
  for (let i = 0; i < 66; i++) script.push(['undo']);
  script.push(['redo'], ['redo']);
  drive(script);
});

wtest('memento history: Lines steps mix in, and trimming hands the floor the dropped view', () => {
  const script = [['reset', memento(-1, 0, 0)], ['push', [line(9)]]];
  for (let i = 0; i < 70; i++) script.push(['push', memento(i, i % 4, 1)]);
  for (let i = 0; i < 66; i++) script.push(['undo']);
  script.push(['redo'], ['push', [line(1), line(2)]], ['undo'], ['redo']);
  drive(script);
});
