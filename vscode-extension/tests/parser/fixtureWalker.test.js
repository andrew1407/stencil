// The shared .stc corpus, replayed through the parser copies in src/parser/. The same file
// core/tests/scriptFixtures.test.cpp and browser/tests/scriptFixtures.test.js read: if this
// tree's copies ever drift in behaviour, a case here says so before a user sees it.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { parseScript, scriptDiagnostics, scriptDump } from '../../src/parser/index.js';

const CASES_PATH = fileURLToPath(
  new URL('../../../browser/js/config/script/fixtures/cases.txt', import.meta.url),
);

/* → [{ name, script, dump, diagnostics }], by the splitting rules in the corpus's
 * _schema.md: a section ends at the next marker and the blank line before it is the file's,
 * not the case's. */
const readScriptCases = () => {
  const cases = [];
  let current = null;
  let section = null;

  const flush = () => {
    if (!current) return;
    for (const key of ['script', 'dump', 'diagnostics']) {
      if (current[key] === null) continue;
      const body = current[key];
      while (body.length > 0 && body[body.length - 1] === '') body.pop();
      current[key] = body.length > 0 ? `${body.join('\n')}\n` : '';
    }
    if (current.diagnostics === null) current.diagnostics = '';
    cases.push(current);
  };

  for (const line of readFileSync(CASES_PATH, 'utf8').split('\n')) {
    if (line.startsWith('=== ')) {
      flush();
      current = { name: line.slice(4).trim(), script: null, dump: null, diagnostics: null };
      section = null;
      continue;
    }
    if (!current) continue;
    if (line === '--- script') { current.script = []; section = 'script'; continue; }
    if (line === '--- dump') { current.dump = []; section = 'dump'; continue; }
    if (line === '--- diagnostics') { current.diagnostics = []; section = 'diagnostics'; continue; }
    if (section) current[section].push(line);
  }
  flush();
  return cases;
};

const CASES = readScriptCases();

test('the shared corpus is present', () => {
  assert.ok(CASES.length >= 40, `the corpus shrank to ${CASES.length} — a case was deleted?`);
});

for (const c of CASES) {
  test(`script fixture: ${c.name}`, () => {
    const program = parseScript(c.script);
    assert.equal(scriptDump(program), c.dump, `dump mismatch for ${c.name}`);
    assert.equal(scriptDiagnostics(program), c.diagnostics, `diagnostic mismatch for ${c.name}`);
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
