// js/core/abi/coreMarshal.js over a fake core whose heap can run out: a 0 from _malloc throws
// instead of aliasing the heap's start, a failed scratch grow is not cached, a wasm op the
// heap cannot serve is answered by its JS twin, and an op or handle that fails midway frees what it took.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createMarshal, heapAlloc, heapAllocAll, guardHeapOps, withJsFallback, CoreHeapError }
  from '../../../js/core/abi/coreMarshal.js';
import { buildPageOps } from '../../../js/core/abi/corePageOps.js';
import { buildStateOps } from '../../../js/core/abi/coreHandles.js';

// A heap of `capacity` bytes; every live block is tracked so a leak shows.
const fakeCore = (capacity) => {
  const live = new Map();
  let next = 8;
  const core = {
    live,
    HEAPU8: new Uint8Array(1 << 16),
    _malloc: (n) => {
      const used = [...live.values()].reduce((a, b) => a + b, 0);
      if (used + n > core.capacity) return 0;
      const p = next; next += n; live.set(p, n); return p;
    },
    _free: (p) => { live.delete(p); },
    capacity,
  };
  core.HEAPF64 = { buffer: core.HEAPU8.buffer };
  return core;
};

test('heapAlloc throws a CoreHeapError on a 0 pointer', () => {
  const core = fakeCore(16);
  assert.ok(heapAlloc(core, 16) > 0);
  assert.throws(() => heapAlloc(core, 1), CoreHeapError);
});

test('heapAllocAll frees what it took before the failing buffer', () => {
  const core = fakeCore(24);
  assert.throws(() => heapAllocAll(core, [8, 8, 16]), CoreHeapError);
  assert.equal(core.live.size, 0, 'nothing leaks');
  assert.equal(heapAllocAll(core, [8, 16]).length, 2);
});

test('the marshal helpers throw rather than hand the op a null pointer', () => {
  const m = createMarshal(fakeCore(0));
  assert.throws(() => m.withCString('abc', () => 'ran'), CoreHeapError);
  assert.throws(() => m.allocPoints([{ x: 1, y: 2 }]), CoreHeapError);
  assert.throws(() => m.withRectOut(() => {}), CoreHeapError);
});

test('a failed scratch grow caches nothing: the next call grows again, never reusing ptr 0', () => {
  const core = fakeCore(64);
  const m = createMarshal(core);
  const small = m.pixelScratch(32);
  assert.ok(small > 0);
  assert.equal(m.pixelScratch(16), small, 'a smaller request reuses the buffer');
  assert.throws(() => m.pixelScratch(128), CoreHeapError);
  assert.equal(core.live.size, 0, 'the old buffer was released for the grow');
  assert.ok(m.pixelScratch(16) > 0, 'no {ptr: 0} was cached as a valid scratch: it grows again');
  core.capacity = 256;
  assert.ok(m.pixelScratch(128) > 0);
});

test('a heap failure retires the guarded ops, and a bound call is answered by its JS twin', () => {
  const retired = [];
  const ops = guardHeapOps({
    ok: () => 'wasm',
    oom: () => { throw new CoreHeapError(8); },
    bug: () => { throw new TypeError('boom'); },
  }, (err) => retired.push(err));
  assert.equal(ops.ok(), 'wasm');
  assert.throws(() => ops.bug(), TypeError);
  assert.equal(retired.length, 0, 'only a heap failure retires the core');
  assert.throws(() => ops.oom(), CoreHeapError);
  assert.equal(retired.length, 1);

  const twin = (x) => `js:${x}`;
  assert.equal(withJsFallback(() => ops.oom, twin)(1), 'js:1');
  assert.equal(withJsFallback(() => null, twin)(2), 'js:2');
  assert.equal(withJsFallback(() => ops.ok, twin)(3), 'wasm');
  assert.throws(() => withJsFallback(() => ops.bug, twin)(4), TypeError, 'a real bug still surfaces');
});

// Every cwrap'd export answers a fresh handle (create) or 0, recording the creates and destroys.
const withExports = (core) => {
  const live = new Set();
  let next = 1;
  core.handles = live;
  core.getValue = () => 0;
  core.cwrap = (name) => (name.endsWith('_create') ? () => { live.add(next); return next++; }
    : name.endsWith('_destroy') ? (h) => { live.delete(h); } : () => 0);
  core.ccall = () => 0;
  return core;
};

test('boundingBoxCenter frees what it took when the heap runs out between its two buffers', () => {
  const core = withExports(fakeCore(40));   // room for the points (32 bytes) or the out slot (16), not both
  const ops = buildPageOps(core, createMarshal(core));
  assert.throws(() => ops.boundingBoxCenter([{ x: 0, y: 0 }, { x: 2, y: 2 }]), CoreHeapError);
  assert.equal(core.live.size, 0, 'nothing leaks');
  core.capacity = 64;
  assert.deepEqual(ops.boundingBoxCenter([{ x: 0, y: 0 }]), { x: 0, y: 0 });
  assert.equal(core.live.size, 0);
});

test('a state handle whose out slot fails takes no core handle; destroy frees both', () => {
  const core = withExports(fakeCore(0));
  const { HoldDrawController, HistoryStack } = buildStateOps(core);
  for (const Klass of [HoldDrawController, HistoryStack]) assert.throws(() => new Klass(), CoreHeapError);
  assert.equal(core.handles.size, 0, 'no handle orphaned by the failed constructor');
  core.capacity = 64;
  const made = [new HoldDrawController(), new HistoryStack()];
  for (const h of made) { h.destroy(); h.destroy(); }
  assert.equal(core.handles.size, 0);
  assert.equal(core.live.size, 0);
});
