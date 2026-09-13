// Walks the shared corpus in js/config/script/fixtures/cases.txt — the same file
// core/tests/scriptFixtures.test.cpp replays. Mirrors core/tests/scriptFixtures.test.cpp.
import test from 'node:test';
import assert from 'node:assert/strict';

import { parseScript, scriptDiagnostics, scriptDump } from '../js/core/script.js';
import { readScriptCases } from './helpers/scriptCases.js';

const CASES = readScriptCases();

test('the script fixture corpus is present', () => {
  assert.ok(CASES.length >= 40, `the corpus shrank to ${CASES.length} — a case was deleted?`);
});

for (const c of CASES) {
  test(`script fixture: ${c.name}`, () => {
    const program = parseScript(c.script);
    assert.equal(scriptDump(program), c.dump, `dump mismatch for ${c.name}`);
    assert.equal(scriptDiagnostics(program), c.diagnostics, `diagnostic mismatch for ${c.name}`);
    // The naming convention is load-bearing: err-* must fail, everything else must not.
    assert.equal(program.hasErrors, c.name.startsWith('err-'), `error expectation for ${c.name}`);
  });
}

test('a parse never throws on truncated input', () => {
  const src = CASES.find((c) => c.name === 'tour-crop').script;
  for (let cut = 0; cut < src.length; cut += 7) {
    const program = parseScript(src.slice(0, cut));
    assert.ok(Array.isArray(program.ops));
  }
});
