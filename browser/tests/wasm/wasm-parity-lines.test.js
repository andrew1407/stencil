// Parity for the core's line-list edits (core/wasmEditApi.cpp) against their JS twins: the
// co-edit merge (layout.js mergeKeepJS ↔ core/state/lineMerge) and the chain edits
// (touch/dragGestures.js ↔ core/geometry/lineChain).
import { test, before } from 'node:test';
import assert from 'node:assert';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { core } from '../../js/core/abi/stencilCore.js';
import { mergeKeepJS, mergeLines } from '../../js/core/layout.js';
import { unchainLine, pullOutPoint } from '../../js/core/touch/dragGestures.js';

const MODULE_BUILT = existsSync(fileURLToPath(new URL('../../js/wasm/stencilCore.js', import.meta.url)));
const wtest = MODULE_BUILT ? test : test.skip;

before(async () => {
  if (!MODULE_BUILT) return;
  const ok = await core.init();
  assert.strictEqual(ok, true, 'wasm core must load in Node (SINGLE_FILE ES module)');
});

const P = (...xy) => xy.map(([x, y]) => ({ x, y }));
const line = (over = {}) => ({ points: P([1, 2], [3.5, -4.25]), color: '#f00', thickness: 2, pointSize: 4,
  style: 'solid', locked: false, fillColor: 'transparent', pointColor: '', ...over });

wtest('merge: the wasm keep mask matches the JS reference key for key', () => {
  const keep = core.op('mergeLinesKeep');
  const server = [line(), line({ color: '#0f0' }), line({ points: P([123456.1, 7]) }), line({ color: 'ünï|code' })];
  const local = [
    line(), line({ points: P([123456.2, 7]) }), line({ pointColor: '#00f' }), line({ thickness: 2.0000001 }),
    { points: P([1, 2], [3.5, -4.25]), color: '#f00' },              // sparse: the core defaults fill in
    { points: P([1, 2], [3.5, -4.25]) }, { points: P([1, 2], [3.5, -4.25]) },   // #FFFF00, twice
    line({ color: 'ünï|code' }), line({ locked: true }), line({ points: P([0.1 + 0.2, -0]) }), line({ points: [] }),
  ];
  assert.deepStrictEqual(keep(server, local), mergeKeepJS(server, local));
  assert.deepStrictEqual(keep([], local), mergeKeepJS([], local));
  assert.deepStrictEqual(keep(server, []), []);
});

wtest('merge: past the codec it declines, and mergeLines still answers from JS', () => {
  const keep = core.op('mergeLinesKeep');
  assert.strictEqual(keep([], [{ points: [null] }]), null);
  assert.deepStrictEqual(mergeLines([line()], [line(), { points: [null] }]).length, 2);
});

wtest('chain: unchain and pull-out edit a line exactly as dragGestures.js does', () => {
  const [wUnchain, wPull] = ['unchainLine', 'pullOutPoint'].map((n) => core.op(n));
  const closed = () => ({ locked: true, fillColor: '#39f', points: P([0, 0], [10, 0], [10, 10], [0, 0]) });
  const rect = () => ({ locked: true, fillColor: '#39f', points: P([0, 0], [10, 0], [10, 10], [0, 10]) });
  const open = () => ({ locked: false, fillColor: 'transparent', points: P([0, 0], [10, 0], [20, 0]) });
  for (const make of [closed, rect, open]) {
    const a = make(), b = make();
    assert.strictEqual(wUnchain(a), unchainLine(b));
    assert.deepStrictEqual(a, b);
  }
  const targets = [
    { kind: 'point', ptIdx: 0 }, { kind: 'point', ptIdx: 1 }, { kind: 'point', ptIdx: 7 }, { kind: 'point', ptIdx: -1 },
    { kind: 'segment', ptIdx: 0, ptIdx2: 1 }, { kind: 'segment', ptIdx: 2, ptIdx2: 3 }, { kind: 'segment', ptIdx: 3, ptIdx2: 9 },
  ];
  for (const make of [closed, rect, open])
    for (const t of targets) {
      const a = make(), b = make();
      assert.strictEqual(wPull(a, t, 14, 6), pullOutPoint(b, t, 14, 6), `${JSON.stringify(t)}: index`);
      assert.deepStrictEqual(a, b, `${JSON.stringify(t)}: line`);
    }
});
