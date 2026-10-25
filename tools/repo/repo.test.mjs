// The tools' shared I/O (io.mjs) and the machine-sized test runner (testRun.mjs).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, rmSync, unlinkSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { blobsAt, listFiles, mapLimit, readAll } from './io.mjs';
import { MAX_WORKERS, floorFor, floorVerdict, tapTotal, testWorkers } from './testRun.mjs';

test('the runner takes half the cores, at least one and at most the cap, unless told otherwise', () => {
  assert.deepEqual([1, 2, 4, 8, 12, 16, 64].map((c) => testWorkers(c, undefined)), [1, 1, 2, 4, 6, 6, 6]);
  assert.equal(MAX_WORKERS, 6);
  assert.equal(testWorkers(16, '3'), 3);
  for (const junk of ['0', '-2', 'many', '']) assert.equal(testWorkers(16, junk), 6, junk);
});

test('the runner hands back node --test\'s exit code', () => {
  const dir = mkdtempSync(path.join(tmpdir(), 'testRun-'));
  try {
    const runner = fileURLToPath(new URL('./testRun.mjs', import.meta.url));
    // A fresh run, not a subtest of this one: node --test marks its children through the env.
    const { NODE_TEST_CONTEXT, ...env } = process.env;
    writeFileSync(path.join(dir, 'ok.test.mjs'), "import { test } from 'node:test'; test('ok', () => {});\n");
    assert.equal(spawnSync(process.execPath, [runner, 'ok.test.mjs'], { cwd: dir, env }).status, 0);
    writeFileSync(path.join(dir, 'bad.test.mjs'), "import { test } from 'node:test'; test('bad', () => { throw new Error('x'); });\n");
    assert.equal(spawnSync(process.execPath, [runner, 'bad.test.mjs'], { cwd: dir, env }).status, 1);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a bare run answers to package.json stencil.testFloor; a scoped run, or no key, to none', () => {
  const dir = mkdtempSync(path.join(tmpdir(), 'testFloor-'));
  try {
    assert.equal(floorFor(dir, []), null);
    writeFileSync(path.join(dir, 'package.json'), '{"stencil":{"testFloor":40}}');
    assert.equal(floorFor(dir, []), 40);
    assert.equal(floorFor(dir, ['tests/one.test.js']), null);
    writeFileSync(path.join(dir, 'package.json'), '{"stencil":{"testFloor":"40"}}');
    assert.equal(floorFor(dir, []), null);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
  assert.equal(tapTotal('TAP version 13\n# tests 12\n# pass 12\n'), 12);
  assert.equal(tapTotal('no summary'), null);
  assert.equal(floorVerdict('pkg', 12, 12), null);
  assert.equal(floorVerdict('pkg', 11, 12), 'pkg suite collapsed to 11 tests, floor is 12');
  assert.match(floorVerdict('pkg', null, 12), /no test total/);
});

test('a bare run that falls below its floor fails naming both numbers', () => {
  const dir = mkdtempSync(path.join(tmpdir(), 'testRunFloor-'));
  try {
    const runner = fileURLToPath(new URL('./testRun.mjs', import.meta.url));
    const { NODE_TEST_CONTEXT, ...env } = process.env;
    mkdirSync(path.join(dir, 'tests'));
    writeFileSync(path.join(dir, 'tests', 'ok.test.mjs'), "import { test } from 'node:test'; test('a', () => {}); test('b', () => {});\n");
    writeFileSync(path.join(dir, 'package.json'), '{"stencil":{"testFloor":2}}');
    assert.equal(spawnSync(process.execPath, [runner], { cwd: dir, env }).status, 0);
    writeFileSync(path.join(dir, 'package.json'), '{"stencil":{"testFloor":3}}');
    const low = spawnSync(process.execPath, [runner], { cwd: dir, env, encoding: 'utf8' });
    assert.equal(low.status, 1);
    assert.match(low.stderr, /collapsed to 2 tests, floor is 3/);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('mapLimit keeps input order and never runs more than its limit at once', async () => {
  let live = 0, peak = 0;
  const out = await mapLimit([5, 1, 4, 2, 3, 0, 6], async (n, i) => {
    peak = Math.max(peak, ++live);
    await new Promise((r) => setTimeout(r, n));
    live--;
    return n * 10 + i;
  }, 3);
  assert.deepEqual(out, [50, 11, 42, 23, 34, 5, 66]);
  assert.equal(peak, 3);
  assert.deepEqual(await mapLimit([], async () => 1), []);
});

test('one git process lists the tree and reads every blob at a ref, a missing one as null', async () => {
  const root = mkdtempSync(path.join(tmpdir(), 'repoIo-'));
  const g = (...a) => execFileSync('git', ['-C', root, ...a], { stdio: 'ignore' });
  try {
    g('init', '-q');
    writeFileSync(path.join(root, 'a.js'), 'one\n');
    writeFileSync(path.join(root, 'b c.js'), 'två\n');
    writeFileSync(path.join(root, 'gone.js'), 'x');
    g('add', '.');
    g('-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-qm', 'c');
    unlinkSync(path.join(root, 'gone.js'));
    writeFileSync(path.join(root, 'new.js'), 'fresh');
    assert.deepEqual((await listFiles(root)).sort(), ['a.js', 'b c.js', 'new.js']);
    assert.deepEqual(await readAll(root, ['a.js', 'new.js']), ['one\n', 'fresh']);
    const then = await blobsAt(root, 'HEAD', ['a.js', 'nope.js', 'b c.js', 'gone.js']);
    assert.deepEqual([...then], [['a.js', 'one\n'], ['nope.js', null], ['b c.js', 'två\n'], ['gone.js', 'x']]);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
