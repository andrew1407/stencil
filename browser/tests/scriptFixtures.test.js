// Walks the shared corpus in js/config/script/fixtures/ — the same files core's
// scriptFixtures.test.cpp replays. Mirrors core/tests/scriptFixtures.test.cpp.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { parseScript, scriptDiagnostics, scriptDump } from '../js/core/script.js';

const DIR = fileURLToPath(new URL('../js/config/script/fixtures/', import.meta.url));
const read = (p) => readFileSync(p, 'utf8');

const CASES = readdirSync(DIR).filter((f) => f.endsWith('.stc')).sort();

test('the script fixture corpus is present', () => {
  assert.ok(CASES.length >= 40, `the corpus shrank to ${CASES.length} — a fixture was deleted?`);
});

for (const file of CASES) {
  const name = file.slice(0, -4);
  test(`script fixture: ${name}`, () => {
    const program = parseScript(read(DIR + file));

    const dumpPath = `${DIR + name}.dump.txt`;
    assert.ok(existsSync(dumpPath), `no .dump.txt for ${name}`);
    assert.equal(scriptDump(program), read(dumpPath), `dump mismatch for ${name}`);

    const diagPath = `${DIR + name}.diag.txt`;
    const expected = existsSync(diagPath) ? read(diagPath) : '';
    assert.equal(scriptDiagnostics(program), expected, `diagnostic mismatch for ${name}`);

    // The naming convention is load-bearing: err-* must fail, everything else must not.
    assert.equal(program.hasErrors, name.startsWith('err-'), `error expectation for ${name}`);
  });
}

test('a parse never throws on truncated input', () => {
  const src = read(`${DIR}tour-crop.stc`);
  for (let cut = 0; cut < src.length; cut += 7) {
    const program = parseScript(src.slice(0, cut));
    assert.ok(Array.isArray(program.ops));
  }
});
