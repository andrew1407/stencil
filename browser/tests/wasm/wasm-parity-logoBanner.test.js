// Parity for the console logo banner: the wasm rows (js/wasm/stencilCore.js, over the heap as one
// string) against the JS reference, every mark at both sizes, boxed and bare.
import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { core } from '../../js/core/abi/stencilCore.js';
import { MARKS, logoBanner } from '../../js/core/format/logoBanner.js';

const MODULE_BUILT = ['stencilCore.js', 'stencilCore.wasm'].every((f) => existsSync(fileURLToPath(new URL(`../../js/wasm/${f}`, import.meta.url))));
const wtest = MODULE_BUILT ? test : test.skip;

// Captured before wasm is installed, so these are the JS fallback's rows.
const jsRef = [];
for (const mark of [...MARKS, 'Q']) for (const compact of [false, true]) for (const boxed of [false, true])
  jsRef.push({ mark, compact, boxed, rows: logoBanner(mark, compact, boxed) });

before(async () => {
  if (!MODULE_BUILT) return;
  assert.equal(await core.init(), true, 'wasm core must load in Node');
});

wtest('logoBanner: wasm rows match the JS reference for every mark, size and box', () => {
  const fn = core.op('logoBanner');
  for (const c of jsRef) assert.deepEqual(fn(c.mark, c.compact, c.boxed), c.rows, `${c.mark} compact=${c.compact} boxed=${c.boxed}`);
});
