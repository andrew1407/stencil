// Unit tests for the docPaths.mjs beside this file over a synthetic tree, plus the live repo under --check.
// Run: node --test tools/docPaths.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { deadRefs, dropIgnored, extractRefs, isCandidate, makeUniverse, resolves } from './docPaths.mjs';

const u = makeUniverse([
  'browser/js/ui/tip/controlTooltip.js',
  'common/config/llm/opRegistry.json',
  'browser/tests/wasm/wasm-parity.test.js',
  'browser/ARCHITECTURE.md',
  'core/parse/formulaParser.hpp',
  'core/parse/formulaParser.cpp',
  'bot/src/Stencil.TelegramBot.Application/Llm/Plan/OpPlanParser.cs',
  'cli/src/pipeline/oneshot.zig',
  'server/internal/hub/hub.go',
  'contracts/llm/llm-contract.md',
  'desktop/tests/app/actions/MainWindow.menus.gui.cpp',
  'browser-extension/tests/llm/llmClient.test.js',
]);
const dead = (doc, md) => deadRefs(doc, md, u).map((r) => r.token);

test('code spans and relative links are extracted; fenced blocks are not', () => {
  const refs = extractRefs('see `a/b.js` and [x](../c.md)\n```\n`d/e.js`\n```\n`cd x && node f/g.js`');
  assert.deepEqual(refs.map((r) => [r.token, r.kind, r.line]),
    [['a/b.js', 'code', 1], ['../c.md', 'link', 1], ['cd', 'code', 5], ['x', 'code', 5],
      ['&&', 'code', 5], ['node', 'code', 5], ['f/g.js', 'code', 5]]);
});

test('prose, URLs, MIME types and absolute paths are not candidates', () => {
  for (const w of ['and/or', 'application/json', 'image/png', 'http://localhost:8080/', 'ziglang.org/builds/',
    '/opt/homebrew/bin', '~/.ssh/', '$HOME/x', 'b&w/sepia', '--test', '{output_dir}/result.png']) {
    assert.equal(isCandidate(w, 'README.md', u), false, w);
  }
  assert.equal(isCandidate('ui/controlTooltip', 'browser/ARCHITECTURE.md', u), true);
  assert.equal(isCandidate('src/x.json', 'README.md', u), true);
});

test('a path resolves from the root, the doc, the surface root and its js/ + src/', () => {
  assert.ok(resolves('browser/js/ui/tip/controlTooltip.js', 'README.md', u));
  assert.ok(resolves('ui/tip/controlTooltip.js', 'browser/ARCHITECTURE.md', u));
  assert.ok(resolves('pipeline/oneshot.zig', 'cli/ARCHITECTURE.md', u));
  assert.ok(resolves('core/parse/formulaParser', 'README.md', u), 'a module stem');
  assert.ok(resolves('core/parse/formulaParser.{hpp,cpp}', 'README.md', u), 'brace alternatives');
  assert.ok(resolves('pipeline/oneshot.run', 'cli/ARCHITECTURE.md', u), 'module.member');
  assert.ok(resolves('Application/Llm/Plan/OpPlanParser*.cs', '.claude/x.md', u), 'namespace abbreviation');
  assert.ok(resolves('desktop/tests/…/MainWindow.<area>.gui.cpp', 'CLAUDE.md', u), 'ellipsis + placeholder');
  assert.ok(resolves('./internal/hub/...', 'CLAUDE.md', u), 'a Go package pattern');
  assert.ok(resolves('common/config/', 'README.md', u), 'a directory');
});

test('a moved or renamed path is dead', () => {
  assert.deepEqual(dead('browser/ARCHITECTURE.md', '`ui/controlTooltip` and `ui/tip/controlTooltip`'),
    ['ui/controlTooltip']);
  assert.deepEqual(dead('README.md', '`browser/tests/wasm-parity*.test.js` `browser/tests/wasm/*.test.js`'),
    ['browser/tests/wasm-parity*.test.js']);
  assert.deepEqual(dead('README.md', '`core/parse/formulaParser.{hpp,js}`'), ['core/parse/formulaParser.{hpp,js}']);
  assert.deepEqual(dead('README.md', '`Application/Llm/OpPlanParser*.cs`'), ['Application/Llm/OpPlanParser*.cs']);
});

test("a test file's name is no module stem: the source it once tested may be gone", () => {
  assert.deepEqual(dead('ARCHITECTURE.md', '`llm/llmClient` `tests/llm/llmClient.test.js`'), ['llm/llmClient']);
  assert.deepEqual(dead('desktop/README.md', '`tests/app/actions/MainWindow` `tests/app/actions/MainWindow.menus.gui`'),
    ['tests/app/actions/MainWindow']);
});

test('links resolve relative to the doc and nowhere else', () => {
  assert.deepEqual(dead('contracts/llm/llm-contract.md',
    '[a](../common/config/llm/opRegistry.json) [b](../../common/config/llm/opRegistry.json) '
    + '[c](#anchor) [d](https://example.com/x)'), ['../common/config/llm/opRegistry.json']);
});

test('build output and the declared non-paths pass', () => {
  assert.deepEqual(dead('cli/README.md', '`zig-out/bin/stencil` `browser/js/wasm/stencilCore.js` `core/build/x`'), []);
  assert.deepEqual(dead('.claude/rules/architecture.md', '`parts/` `misc/`'), []);
});

test('a gitignored directory absent from disk is no dead path', () => {
  const root = mkdtempSync(path.join(tmpdir(), 'docPaths-'));
  try {
    execFileSync('git', ['init', '-q', root]);
    writeFileSync(path.join(root, '.gitignore'), 'build-asan/\nsrv/vendor/\n');
    const dead = [{ doc: 'core/README.md', token: 'core/build-asan' }, { doc: 'srv/README.md', token: 'vendor/' },
      { doc: 'srv/README.md', token: 'gone.go' }];
    assert.deepEqual(dropIgnored(root, dead).map((d) => d.token), ['gone.go']);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('the repo has no dead doc path', () => {
  const tool = fileURLToPath(new URL('./docPaths.mjs', import.meta.url));
  let out = '';
  try {
    out = execFileSync(process.execPath, [tool, '--check'], { encoding: 'utf8' });
  } catch (err) {
    assert.fail(`dead paths in the docs — fix the path, or declare a non-path in ALLOW:\n${err.stdout}`);
  }
  assert.match(out, /docPaths: 0 dead paths/);
});
