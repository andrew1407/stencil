// The built module carries no op-plan validator: core/wasmOpplanApi.cpp is linked into stencil_wasm
// unexported, so wasm-ld drops it — no `_stencil_opplan*` on the Module, and none of the diagnostic
// codes only core/opplan writes left in the binary's data. Self-skips without the artifact, like
// its siblings; the export lists' half is tests/core/abi/opplanOutOfWasm.test.js.
import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { opplanWasmExports, diagCodes, coreGroups } from '../helpers/opplanAbi.js';

const ARTIFACT = new URL('../../js/wasm/stencilCore.js', import.meta.url);
const BINARY = new URL('../../js/wasm/stencilCore.wasm', import.meta.url);
const MODULE_BUILT = existsSync(fileURLToPath(ARTIFACT)) && existsSync(fileURLToPath(BINARY));
const wtest = MODULE_BUILT ? test : test.skip;

let mod = null;
let binary = null;
before(async () => {
  if (!MODULE_BUILT) return;
  binary = readFileSync(BINARY);
  const { default: createStencilCore } = await import(ARTIFACT.href);
  mod = await createStencilCore();
});

wtest('the Module offers no op-plan or JSON function', () => {
  assert.equal(typeof mod._stencil_parseHex, 'function', 'the loaded Module shows its exports');
  const offered = Object.keys(mod).filter((k) => typeof mod[k] === 'function' && /opplan|json/i.test(k));
  assert.deepEqual(offered, [], 'an op-plan or JSON export reached the module');
  for (const sym of opplanWasmExports()) assert.equal(mod[`_${sym}`], undefined, `_${sym} is exported`);
});

wtest('the binary holds none of the codes only the op-plan walker writes', () => {
  const elsewhere = diagCodes(coreGroups().filter((group) => group !== 'opplan' && group !== 'json'));
  const walkerOnly = [...diagCodes(['opplan'])].filter((code) => !elsewhere.has(code));
  const scriptCodes = [...diagCodes(['script'])];
  assert.ok(walkerOnly.length > 0 && scriptCodes.length > 0, 'read the codes out of core/');
  const held = (code) => binary.includes(Buffer.from(code, 'latin1'));
  assert.ok(scriptCodes.some(held), 'the exported script engine keeps its codes, so the scan reads the data');
  assert.deepEqual(walkerOnly.filter(held), [], 'the op-plan walker was linked into the module');
});
