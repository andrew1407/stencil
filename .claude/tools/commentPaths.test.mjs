// Unit tests for the commentPaths.mjs beside this file over synthetic sources, plus the live repo under --check.
// Run: node --test .claude/tools/commentPaths.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { makeUniverse } from './docPaths.mjs';
import { deadRefs, refsOf, scanCss, wordOf } from './commentPaths.mjs';

const u = makeUniverse([
  'browser/js/ui/motion/motionPrefs.js',
  'browser/js/llm/plan/opPlan.js',
  'browser/css/layout/topbar.css',
  'browser-extension/src/lib/prefs/pins.js',
  'desktop/src/support/motion/scrollReveal.hpp',
  'core/raster/rasterize.cpp',
  'pystencil/pystencil/llm/wire.py',
]);
const dead = (file, src) => deadRefs(file, src, u).map((r) => `${r.line} ${r.token}`);

test('only comment text is read, never code or a string', () => {
  const src = "const a = 'ui/gone.js'; // ui/motion/motionPrefs.js\n/* x/y.js */ f('z/w.js');\n";
  assert.deepEqual(refsOf(src, 'js').map((r) => [r.line, r.token]),
    [[1, 'ui/motion/motionPrefs.js'], [2, 'x/y.js']]);
  assert.deepEqual(refsOf('s = "a/b.py"  # c/d.py\n', 'py').map((r) => r.token), ['c/d.py']);
});

test('CSS comments are block-only: a // inside url() is content', () => {
  const src = 'a { background: url(//cdn/x.png); } /* css/layout/topbar.css */';
  assert.deepEqual(scanCss(src).map((s) => src.slice(s.start, s.end)), ['/* css/layout/topbar.css */']);
});

test('a word sheds its wrapping, a :line and an #anchor, but keeps a brace run whole', () => {
  assert.equal(wordOf('(ui/motion/motionPrefs.js).'), 'ui/motion/motionPrefs.js');
  assert.equal(wordOf('`core/raster/rasterize.cpp:131-154`,'), 'core/raster/rasterize.cpp');
  assert.equal(wordOf('llm-contract.md#section'), 'llm-contract.md');
  assert.equal(wordOf('core/raster/rasterize.{hpp,cpp}'), 'core/raster/rasterize.{hpp,cpp}');
});

test('a moved file is dead from any surface; its current home resolves', () => {
  assert.deepEqual(dead('browser/js/index.js', '// ui/prefs.js and ui/motion/motionPrefs.js\n'), ['1 ui/prefs.js']);
  assert.deepEqual(dead('browser-extension/src/content/x.js', '// must match lib/pins.js\n// lib/prefs/pins.js\n'),
    ['1 lib/pins.js']);
  assert.deepEqual(dead('core/raster/x.hpp', '// browser twin: browser/js/llm/plan/opPlan.js\n'), []);
  assert.deepEqual(dead('pystencil/tests/t.py', '# llm/wire.py WIRES\n'), []);
});

test('a folder, a glob and a whole placeholder resolve; prose and half placeholders are no paths', () => {
  assert.deepEqual(dead('desktop/src/x.cpp', '// browser/css/layout/ and support/motion/*.hpp\n'), []);
  assert.deepEqual(dead('browser/js/x.js', '// js/llm/plan/<name>.js\n'), []);
  assert.deepEqual(dead('cli/src/x.zig', '// "<dir path>/<name>.json" and/or w/h\n'), []);
});

test('the repo has no dead comment path', () => {
  const tool = fileURLToPath(new URL('./commentPaths.mjs', import.meta.url));
  let out = '';
  try {
    out = execFileSync(process.execPath, [tool, '--check'], { encoding: 'utf8' });
  } catch (err) {
    assert.fail(`dead paths in code comments — fix the path, or declare a non-path in ALLOW:\n${err.stdout}`);
  }
  assert.match(out, /commentPaths: 0 dead paths/);
});
