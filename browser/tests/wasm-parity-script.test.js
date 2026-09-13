// Parity coverage for the .stc engine (core/script/, reached through the handle wrappers
// in js/core/scriptHandles.js). wasm-parity.test.js pins the pure ops; this one pins the
// language: the compiled C++ and the hand-written JS fallback must lower every fixture in
// the shared corpus to the same op stream and report the same diagnostics for it. Node
// loads the SINGLE_FILE ES module directly.
import { test, before } from 'node:test';
import assert from 'node:assert';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { core } from '../js/core/stencilCore.js';
import { parseScript, scriptDiagnostics, scriptDump } from '../js/core/script.js';

// Generated artifact (gitignored) — skip rather than fail when it has not been built.
const MODULE_BUILT = existsSync(fileURLToPath(new URL('../js/wasm/stencilCore.js', import.meta.url)));
const wtest = MODULE_BUILT ? test : test.skip;

const DIR = fileURLToPath(new URL('../js/config/script/fixtures/', import.meta.url));
const CASES = readdirSync(DIR).filter((f) => f.endsWith('.stc')).sort();
const source = (f) => readFileSync(DIR + f, 'utf8');

// Captured BEFORE wasm is installed, so these are the hand-written fallback's results.
const jsRef = CASES.map((f) => {
  const p = parseScript(source(f));
  return { dump: scriptDump(p), diags: scriptDiagnostics(p), errors: p.errorCount };
});

before(async () => {
  if (!MODULE_BUILT) return;
  const ok = await core.init();
  assert.strictEqual(ok, true, 'wasm core must load in Node (SINGLE_FILE ES module)');
});

wtest('the corpus is what both sides are measured against', () => {
  assert.ok(CASES.length >= 40, `the corpus shrank to ${CASES.length}`);
});

wtest('wasm lowers every fixture exactly as the JS fallback does', () => {
  CASES.forEach((file, i) => {
    const program = parseScript(source(file));
    assert.strictEqual(scriptDump(program), jsRef[i].dump, `dump ${file}`);
    assert.strictEqual(scriptDiagnostics(program), jsRef[i].diags, `diagnostics ${file}`);
    assert.strictEqual(program.errorCount, jsRef[i].errors, `errorCount ${file}`);
  });
});

wtest('the blocks and ops cross the ABI with their structure intact', () => {
  const program = parseScript(source('tour-multi-source.stc'));
  assert.equal(program.blocks.length, 5);
  assert.equal(program.blocks[0].kind, 'url');
  assert.equal(program.blocks[3].kind, 'dir');
  assert.equal(program.blocks[4].kind, 'glob');
  const crop = parseScript('@source a.png:\n  @crop 10%\n').ops[1];
  assert.deepStrictEqual(crop.toks, ['10%', '-10%', '10%', '-10%']);
  assert.equal(crop.editIndex, 1);
});

wtest('the token stream crosses with the spans an editor colours by', () => {
  const program = parseScript('@source a.png:\n  @filter #ccc # tail\n');
  const kinds = program.tokens.map((t) => t.kind);
  assert.ok(kinds.includes('directive'));
  assert.ok(kinds.includes('color'));
  assert.ok(kinds.includes('comment'));
  for (const t of program.tokens) {
    assert.ok(t.line >= 1 && t.col >= 1 && t.len >= 0, 'every token carries a usable span');
  }
});

wtest('the wasm handle is released, so repeated parses do not leak', () => {
  const src = source('tour-crop.stc');
  const first = scriptDump(parseScript(src));
  for (let i = 0; i < 200; i += 1) parseScript(src);
  assert.strictEqual(scriptDump(parseScript(src)), first);
});
