// The op-plan validator stays out of the wasm build: core/wasmOpplanApi.cpp is compiled into
// stencil_wasm but no export list names it, so wasm-ld drops it and the browser, which validates
// plans with its JS twin (js/llm/plan/), pays nothing for it. The loaded module's half of this
// guard is tests/wasm/wasm-exports-opplan.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { RUNTIME_EXPORTS, PARITY_EXPORTS } from '../../../js/core/abi/coreExports.js';
import { core } from '../../../js/core/abi/stencilCore.js';
import { opplanWasmExports, readCore } from '../../helpers/opplanAbi.js';

const OPPLAN_OR_JSON = /opplan|json/i;
const ABI = opplanWasmExports();
const offending = (syms) => syms.filter((sym) => ABI.includes(sym) || OPPLAN_OR_JSON.test(sym));

const JS = resolve(dirname(fileURLToPath(import.meta.url)), '../../../js');
const walk = (dir, out = []) => {
  for (const name of readdirSync(dir)) {
    const full = join(dir, name);
    if (statSync(full).isDirectory()) { if (name !== 'wasm') walk(full, out); }
    else if (name.endsWith('.js')) out.push(full);
  }
  return out;
};

test('the op-plan ABI has a wasm spelling, and EXPORTED_FUNCTIONS names none of it', () => {
  assert.ok(ABI.includes('stencil_opplanParse') && ABI.includes('stencil_opplanSchemaCreate'),
    `read the op-plan ABI out of abi/opplanShared.inc: ${ABI.join(', ')}`);
  const list = /-sEXPORTED_FUNCTIONS=\[([^\]]*)\]/.exec(readCore('CMakeLists.txt'))?.[1] ?? '';
  const exported = [...list.matchAll(/'_(\w+)'/g)].map((m) => m[1]);
  assert.ok(exported.includes('stencil_parseHex') && exported.includes('stencil_scriptParse'),
    'read the wasm export list out of core/CMakeLists.txt');
  assert.deepEqual(offending(exported), [], 'an op-plan or JSON export keeps the validator in the module');
});

test('the browser asks the wasm core for no op-plan or JSON export', () => {
  const lists = { RUNTIME_EXPORTS, PARITY_EXPORTS: Object.values(PARITY_EXPORTS).flat() };
  for (const [name, syms] of Object.entries(lists)) {
    assert.ok(syms.includes('stencil_parseHex') || syms.includes('stencil_distToSegment'), `${name} was read`);
    assert.deepEqual(offending(syms), [], `${name} requires an op-plan or JSON export`);
  }
  assert.deepEqual(core.opNames.filter((op) => OPPLAN_OR_JSON.test(op)), [], 'core.opNames offers an op-plan op');
  // Nor does a module wrap one by name past those lists (a cwrap or ccall of its own).
  const spelled = walk(JS).flatMap((file) => [...readFileSync(file, 'utf8').matchAll(/\b_?(stencil_\w+)/g)]
    .map((m) => `${file.slice(JS.length + 1)}: ${m[1]}`));
  assert.ok(spelled.some((s) => s.endsWith(': stencil_scriptParse')), 'the scan sees the modules that wrap exports');
  assert.deepEqual(spelled.filter((s) => offending([s.split(': ')[1]]).length), []);
});
