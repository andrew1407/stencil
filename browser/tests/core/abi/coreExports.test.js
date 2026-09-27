// The wasm export split: every op the app reaches at runtime is a required export, and a stale
// artifact that lacks only a parity-only wrapper keeps every other op.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { RUNTIME_EXPORTS, PARITY_EXPORTS, missingExports, droppedParityOps } from '../../../js/core/abi/coreExports.js';

const JS = resolve(dirname(fileURLToPath(import.meta.url)), '../../../js');
const walk = (dir, out = []) => {
  for (const name of readdirSync(dir)) {
    const full = join(dir, name);
    if (statSync(full).isDirectory()) { if (name !== 'wasm' && name !== 'abi') walk(full, out); }
    else if (name.endsWith('.js')) out.push(full);
  }
  return out;
};

test('every core.op / core.bind the app calls has its export in RUNTIME_EXPORTS', () => {
  const used = new Set();
  for (const file of walk(JS))
    for (const m of readFileSync(file, 'utf8').matchAll(/core\.(?:op|bind)\('([A-Za-z]+)'/g)) used.add(m[1]);
  assert.ok(used.size > 10, 'the scan found the runtime ops');
  for (const op of used) assert.ok(RUNTIME_EXPORTS.includes(`stencil_${op}`), `${op} is reached at runtime`);
  for (const op of ['distToSegment', 'pageFormats', 'pixelToPageRaw'])
    assert.ok(!used.has(op), `${op} is parity-only, so the app must not reach it`);
});

test('a module missing only a parity-only export drops that op and nothing else', () => {
  const all = [...RUNTIME_EXPORTS, ...Object.values(PARITY_EXPORTS).flat()];
  const mod = Object.fromEntries(all.map((sym) => [`_${sym}`, () => 0]));
  assert.deepEqual(missingExports(mod, RUNTIME_EXPORTS), []);
  assert.deepEqual(droppedParityOps(mod), []);
  delete mod._stencil_pageFormats;
  delete mod._stencil_history_undo;
  assert.deepEqual(missingExports(mod, RUNTIME_EXPORTS), [], 'the runtime ops survive');
  assert.deepEqual(droppedParityOps(mod), ['pageFormats', 'state']);
});

test('a missing runtime export is reported, which rejects the core', () => {
  const mod = Object.fromEntries(RUNTIME_EXPORTS.map((sym) => [`_${sym}`, () => 0]));
  delete mod._stencil_pageDimensions;
  assert.deepEqual(missingExports(mod, RUNTIME_EXPORTS), ['stencil_pageDimensions']);
});
