// coreLists.mjs: the live tree holds cli/build.zig and pystencil/build.py to core/CMakeLists.txt,
// and a dropped, added or repeated entry is reported, then rewritten from CMake.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { cpSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { cmakeLists, pyLists, run, zigLists } from './coreLists.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const FILES = ['core/CMakeLists.txt', 'cli/build.zig', 'pystencil/build.py'];

const scratchTree = () => {
  const dir = mkdtempSync(path.join(tmpdir(), 'coreLists-'));
  for (const rel of FILES) {
    mkdirSync(path.join(dir, path.dirname(rel)), { recursive: true });
    cpSync(path.join(ROOT, rel), path.join(dir, rel));
  }
  return dir;
};

const edit = (dir, rel, from, to) => {
  const file = path.join(dir, rel);
  const text = readFileSync(file, 'utf8');
  assert.ok(text.includes(from), `${rel} holds ${from}`);
  writeFileSync(file, text.replace(from, to));
};

test('the live tree: the zig and py core lists name exactly what CMake compiles', () => {
  assert.deepEqual(run('--check').problems, []);
  const want = cmakeLists(readFileSync(path.join(ROOT, FILES[0]), 'utf8'));
  assert.ok(want.sources.length >= 50 && want.sources.includes('opplan/planWalk.cpp'));
  assert.ok(want.includeDirs.includes('.') && want.includeDirs.includes('script/program'));
  assert.ok(!want.sources.some((f) => /wasm\w*Api\.cpp$/.test(f)), 'the wasm ABI files stay out of the shared list');
});

test('a dropped, an extra and a repeated entry are each named, and --write restores CMake\'s lists', () => {
  const dir = scratchTree();
  try {
    edit(dir, 'cli/build.zig', '    "raster/markers.cpp",\n', '');
    edit(dir, 'pystencil/build.py', '  "json/jsText.cpp",\n', '  "json/jsText.cpp",\n  "json/jsText.cpp",\n  "json/gone.cpp",\n');
    edit(dir, 'pystencil/build.py', '"opplan"]', '"opplan", "extra"]');
    const { problems } = run('--check', dir);
    assert.deepEqual(problems, [
      'cli/build.zig sources: lacks raster/markers.cpp',
      'pystencil/build.py sources: adds json/gone.cpp; repeats json/jsText.cpp',
      'pystencil/build.py includeDirs: adds extra',
    ]);
    assert.equal(run('--write', dir).wrote.length, 3);
    assert.deepEqual(run('--check', dir).problems, []);
    const want = cmakeLists(readFileSync(path.join(dir, FILES[0]), 'utf8'));
    assert.deepEqual(zigLists(readFileSync(path.join(dir, FILES[1]), 'utf8')), want);
    assert.deepEqual(pyLists(readFileSync(path.join(dir, FILES[2]), 'utf8')), want);
    assert.ok(readFileSync(path.join(dir, FILES[1]), 'utf8').includes('    "cliApi.cpp",\n};'), 'the CLI keeps its ABI file last');
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
