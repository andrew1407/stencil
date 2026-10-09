// Unit tests for the caps.mjs beside this file over synthetic inputs, plus the live repo under --check.
// Run: node --test tools/caps.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { breaches, commentLines, folderUnits, isSource, lineCount, unitOf } from './caps.mjs';

test('lineCount counts lines as wc does, a missing final newline included', () => {
  assert.equal(lineCount(''), 0);
  assert.equal(lineCount('a\n'), 1);
  assert.equal(lineCount('a\nb'), 2);
  assert.equal(lineCount('a\n\nb\n'), 3);
});

test('vendored, generated and build output are not sources', () => {
  for (const f of ['core/third_party/doctest.h', 'server/vendor/x/y.go', 'cli/zig-pkg/a/stb_image.h',
    'common/fixtures/llm/opPlan/generated/x.js', 'vscode-extension/typings/stencil.d.ts',
    'browser/js/wasm/stencilCore.js', 'desktop/build/moc_x.cpp', 'browser/README.md']) {
    assert.equal(isSource(f), false, f);
  }
  for (const f of ['browser/js/ui/a.js', 'browser/js/ui/a.d.ts', 'desktop/src/a.qss', 'cli/src/a.zig']) {
    assert.equal(isSource(f), true, f);
  }
});

test('a module and its .d.ts, a header and its bodies, are one unit each', () => {
  const headers = new Set(['dragPasteboard', 'guiHelpers']);
  assert.equal(unitOf('ui/a.d.ts', headers), 'a');
  assert.equal(unitOf('ui/a.js', headers), 'a');
  assert.equal(unitOf('support/dragPasteboardMac.mm', headers), 'dragPasteboard');
  assert.equal(unitOf('support/guiHelpersColor.cpp', headers), 'guiHelpersColor');
  assert.equal(unitOf('support/shareMac.mm', headers), 'shareMac');
  const units = folderUnits(['d/a.js', 'd/a.d.ts', 'd/x.hpp', 'd/x.cpp', 'd/xWin.cpp', 'd/b_test.go',
    'd/tests/t.js', 'd/c.go']);
  assert.deepEqual(units.find((u) => u.dir === 'd'), { dir: 'd', units: 3 });
  assert.equal(units.some((u) => u.dir === 'd/tests'), false);
});

test('a leading doc banner is not a body comment; later comment-only lines are', () => {
  const src = '// banner one\n// banner two\n\nimport x from "y";\n// body\nconst a = 1; // trailing\n/* b\n c */\n';
  assert.deepEqual(commentLines(src, 'js'), { body: 3, lines: 8 });
  assert.deepEqual(commentLines('# banner\nx = "#"  # trailing\n# body\n', 'py'), { body: 1, lines: 3 });
  assert.deepEqual(commentLines('/* banner */\na { b: c; }\n/* body */\n', 'css'), { body: 1, lines: 3 });
});

test('breaches names each rule over a synthetic tree', () => {
  const root = mkdtempSync(path.join(tmpdir(), 'caps-'));
  try {
    mkdirSync(path.join(root, 'big'));
    mkdirSync(path.join(root, 'wide'));
    writeFileSync(path.join(root, 'big/long.js'), 'x;\n'.repeat(231));
    writeFileSync(path.join(root, 'big/ok.js'), 'x;\n'.repeat(230));
    const wide = Array.from({ length: 13 }, (_, i) => `wide/m${i}.js`);
    for (const f of wide) writeFileSync(path.join(root, f), 'export const a = 1;\n');
    writeFileSync(path.join(root, 'big/chatty.js'), `x;\n${'// note\n'.repeat(400)}`);
    const found = breaches(root, ['big/long.js', 'big/ok.js', 'big/chatty.js', ...wide]);
    assert.deepEqual(found.map((b) => [b.kind, b.where]).sort(),
      [['comments', 'big'], ['lines', 'big/chatty.js'], ['lines', 'big/long.js'], ['units', 'wide']]);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('the live repo is within every cap', () => {
  const here = path.dirname(fileURLToPath(import.meta.url));
  const out = execFileSync(process.execPath, [path.join(here, 'caps.mjs'), '--check'], { encoding: 'utf8', cwd: here });
  assert.match(out.trim().split('\n').pop(), /^caps: 0 breaches$/);
});
