// Parity coverage for the core's STATEFUL classes (core/wasmStateApi.cpp, reached through the handle wrappers
// in js/core/coreHandles.js). wasm-parity.test.js pins the pure ops; this one pins the classes that own state,
// hand-kept twins until they gained an ABI: the JS implementation and the compiled C++ are driven through one
// script and must agree on every returned event and every observable field, step by step. Node loads the
// SINGLE_FILE ES module directly.
import { test, before } from 'node:test';
import assert from 'node:assert';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { core } from '../js/core/abi/stencilCore.js';
import { HoldDrawController } from '../js/core/draw/holdDraw.js';

// Generated artifact (gitignored) — skip rather than fail when it has not been built.
const MODULE_BUILT = existsSync(fileURLToPath(new URL('../js/wasm/stencilCore.js', import.meta.url)));
const wtest = MODULE_BUILT ? test : test.skip;

before(async () => {
  if (!MODULE_BUILT) return;
  const ok = await core.init();
  assert.strictEqual(ok, true, 'wasm core must load in Node (SINGLE_FILE ES module)');
});

// One script step = an op name plus its args, applied to both controllers.
const OPS = {
  down: (c, [x, y, t]) => c.pointerDown(x, y, t),
  move: (c, [x, y, t]) => c.pointerMove(x, y, t),
  tick: (c, [t]) => c.tick(t),
  up: (c, [t]) => c.pointerUp(t),
  cancel: (c) => { c.cancel(); return null; },
  delay: (c, [ms]) => { c.setHoldDelay(ms); return null; },
};

// Run `script` through the JS twin and the wasm one, asserting they stay identical
// after every step. Returns the JS event log so a case can also assert what happened.
const drive = (script, opts = {}) => {
  const js = new HoldDrawController(opts);
  const wasm = new (core.op('HoldDrawController'))(opts);
  const log = [];
  try {
    script.forEach(([op, ...args], i) => {
      const where = `step ${i} (${op} ${args.join(',')})`;
      const jsOut = OPS[op](js, args);
      const wasmOut = OPS[op](wasm, args);
      assert.deepStrictEqual(wasmOut, jsOut, `${where}: event`);
      assert.strictEqual(wasm.state, js.state, `${where}: state`);
      assert.strictEqual(wasm.active, js.active, `${where}: active`);
      assert.strictEqual(wasm.engaged, js.engaged, `${where}: engaged`);
      assert.strictEqual(wasm.holdDelay, js.holdDelay, `${where}: holdDelay`);
      log.push(jsOut);
    });
  } finally {
    wasm.destroy();
  }
  return log;
};

const types = (log) => log.filter(Boolean).map((e) => e.type);

wtest('holdDraw: the wasm class is installed alongside the pure ops', () => {
  assert.strictEqual(typeof core.op('HoldDrawController'), 'function');
  assert.ok(core.opNames.includes('HoldDrawController'));
});

wtest('holdDraw: a full hold — armed → start → preview → drop → commit', () => {
  const log = drive([
    ['down', 10, 10, 0],
    ['tick', 200],            // before the delay: nothing
    ['move', 13, 12, 250],    // inside the move tolerance: nothing
    ['tick', 500],            // delay reached: start at the press point
    ['move', 60, 10, 520],    // past the re-arm distance: preview + new dwell
    ['tick', 900],            // dwell not yet out
    ['tick', 1100],           // dwell complete: drop at the rest point
    ['up', 1200],
  ]);
  assert.deepStrictEqual(types(log), ['armed', 'start', 'preview', 'drop', 'commit']);
  assert.deepStrictEqual(log[3], { type: 'start', x: 10, y: 10 });
  assert.deepStrictEqual(log[6], { type: 'drop', x: 60, y: 10 });
});

wtest('holdDraw: moving past the tolerance before the hold aborts, and stays aborted', () => {
  const log = drive([
    ['down', 0, 0, 0],
    ['move', 6, 0, 10],       // exactly at the tolerance: not past it
    ['move', 40, 0, 20],
    ['move', 41, 0, 30],      // already aborted: nothing more
    ['tick', 9000],
    ['up', 9001],
  ]);
  assert.deepStrictEqual(types(log), ['armed', 'abort']);
});

wtest('holdDraw: a drop needs the cursor to leave the last dropped point first', () => {
  const log = drive([
    ['down', 0, 0, 0],
    ['tick', 500],            // start at (0,0)
    ['move', 8, 0, 600],      // past the move tolerance but inside re-arm: no drop armed
    ['tick', 2000],
    ['move', 30, 0, 2100],    // now past the re-arm distance
    ['tick', 2600],
    ['up', 2700],
  ]);
  assert.deepStrictEqual(types(log), ['armed', 'start', 'preview', 'preview', 'drop', 'commit']);
});

wtest('holdDraw: cancel and a release that never drew produce no commit', () => {
  const log = drive([
    ['down', 5, 5, 0],
    ['cancel'],
    ['tick', 5000],           // cancelled: the hold timer is gone
    ['up', 5001],
    ['down', 5, 5, 6000],
    ['up', 6100],             // released before the delay
  ]);
  assert.deepStrictEqual(types(log), ['armed', 'armed']);
});

wtest('holdDraw: setHoldDelay retimes the machine and rejects the same inputs on both sides', () => {
  const log = drive([
    ['delay', -1],            // negative: ignored
    ['delay', NaN],           // not a number: ignored
    ['delay', 120],
    ['down', 0, 0, 0],
    ['tick', 100],
    ['tick', 120],            // the new delay, not the 500 default
    ['up', 200],
  ]);
  assert.deepStrictEqual(types(log), ['armed', 'start', 'commit']);
  assert.deepStrictEqual(log[5], { type: 'start', x: 0, y: 0 });
});

wtest('holdDraw: non-default tolerances and a zero delay cross the ABI intact', () => {
  drive([
    ['down', 0, 0, 0],
    ['move', 20, 0, 1],       // inside a 40px tolerance
    ['tick', 0],              // a zero delay fires on the first tick
    ['move', 60, 0, 2],       // inside a 100px re-arm: no drop armed
    ['tick', 3],
    ['move', 200, 0, 4],
    ['tick', 5],
    ['up', 6],
  ], { holdDelay: 0, moveTolerance: 40, rearmDistance: 100 });
});

// A long pseudorandom gesture stream, deterministic (a seeded LCG) and integer-valued, so the two hypot()
// implementations compare distances that are never a rounding away from a tolerance boundary.
wtest('holdDraw: 2000 random gesture steps stay identical step for step', () => {
  let seed = 20260912;
  const rnd = (n) => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };
  const names = ['down', 'move', 'tick', 'up', 'cancel', 'delay'];
  const script = [];
  let t = 0;
  for (let i = 0; i < 2000; i++) {
    t += rnd(400);
    const op = names[rnd(names.length)];
    if (op === 'down' || op === 'move') script.push([op, rnd(120), rnd(120), t]);
    else if (op === 'delay') script.push([op, rnd(3) === 0 ? -rnd(50) : rnd(600)]);
    else if (op === 'cancel') script.push([op]);
    else script.push([op, t]);
  }
  const log = drive(script);
  // The stream must actually exercise the machine, not idle through 2000 no-ops.
  assert.ok(types(log).filter((x) => x === 'start').length > 20, 'expected many holds to complete');
});

// The tolerance tests above straddle the boundaries; these sit exactly ON them, where a
// > / >= drift between the twins would otherwise hide (no random stream lands there).
wtest('holdDraw: distances exactly at the move tolerance and the re-arm distance', () => {
  const log = drive([
    ['down', 0, 0, 0],
    ['tick', 500],            // start at (0,0): rest and last drop are both there
    ['move', 10, 0, 600],     // exactly the re-arm distance: not past it, no drop armed
    ['tick', 1200],
    ['move', 30, 0, 1300],    // past it: armed, new dwell window at (30,0)
    ['move', 36, 0, 1400],    // exactly the move tolerance: the dwell window stands
    ['tick', 1800],           // 500ms since the window opened at 1300 → drop at (30,0)
    ['up', 1900],
  ]);
  assert.deepStrictEqual(types(log), ['armed', 'start', 'preview', 'preview', 'preview', 'drop', 'commit']);
  assert.deepStrictEqual(log[6], { type: 'drop', x: 30, y: 0 });
});

wtest('holdDraw: handles are independent and destroy() releases one for good', () => {
  const Wasm = core.op('HoldDrawController');
  const a = new Wasm({ holdDelay: 500 });
  const b = new Wasm({ holdDelay: 50 });
  try {
    a.pointerDown(0, 0, 0);
    assert.strictEqual(b.state, 'idle');
    assert.strictEqual(b.tick(1000), null);       // b was never pressed
    assert.strictEqual(a.holdDelay, 500);
    assert.strictEqual(b.holdDelay, 50);
    assert.deepStrictEqual(a.tick(500), { type: 'start', x: 0, y: 0 });
    assert.strictEqual(b.state, 'idle');
  } finally {
    a.destroy();
    b.destroy();
  }
  b.destroy();                                     // idempotent
  assert.throws(() => a.state, /destroyed/);
  assert.throws(() => a.tick(1), /destroyed/);
});
