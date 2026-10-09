// js/core/abi/coreMarshal.js over a fake core whose heap can run out: a 0 from _malloc throws
// instead of aliasing the heap's start, a failed scratch grow is not cached, and a wasm op the
// heap cannot serve is answered by its JS twin.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createMarshal, heapAlloc, heapAllocAll, guardHeapOps, withJsFallback, CoreHeapError }
  from '../../../js/core/abi/coreMarshal.js';

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
