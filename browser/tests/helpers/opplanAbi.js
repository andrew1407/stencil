// What the op-plan validator (core/opplan over core/json) would bring into the wasm build, read
// from the C++ sources so a renamed or added export or code is followed without an edit here:
// the exports core/abi/opplanShared.inc spells, and the diagnostic codes a core folder writes.
import { readFileSync, readdirSync, statSync } from 'node:fs';

const CORE = new URL('../../../core/', import.meta.url);
export const readCore = (rel) => readFileSync(new URL(rel, CORE), 'utf8');

// Each `STENCIL_ABI(wasmName, cliName)` definition, spelled as the wasm ABI spells it.
export const opplanWasmExports = () => [...readCore('abi/opplanShared.inc')
  .matchAll(/^[\w\s*]+STENCIL_ABI\((\w+), \w+\)\(/gm)].map((m) => `stencil_${m[1]}`);

const sources = (url) => readdirSync(url).flatMap((name) => {
  const child = new URL(name, url);
  if (statSync(child).isDirectory()) return sources(new URL(`${name}/`, url));
  return /\.(cpp|hpp|inc)$/.test(name) ? [child] : [];
});

// The source groups of core/: every folder but the tests, the vendored header and the build trees.
export const coreGroups = () => readdirSync(CORE)
  .filter((name) => !/^(build.*|tests|third_party)$/.test(name) && statSync(new URL(name, CORE)).isDirectory());

// Every `"E_…"` / `"W_…"` literal under the named core/ groups, the codes their diagnostics carry.
export const diagCodes = (groups) => new Set(groups.flatMap((dir) => sources(new URL(`${dir}/`, CORE)))
  .flatMap((url) => [...readFileSync(url, 'utf8').matchAll(/"([EW]_[A-Z_]+)"/g)].map((m) => m[1])));
