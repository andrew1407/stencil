// Finding the binary: the setting, then STENCIL_CLI, then PATH — and the PATH walk memoized,
// because an open-and-save burst asks for it twice in a row.
import test from 'node:test';
import assert from 'node:assert/strict';
import { chmodSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { installVscodeStub, makeDocument, makeVscode } from '../../helpers/vscodeStub.js';

const withLib = (options, body) => {
  const dir = mkdtempSync(join(tmpdir(), 'stencil-vsce-cli-'));
  const { vscode } = makeVscode(options);
  const host = installVscodeStub(vscode);
  try {
    return body({
      dir, vscode,
      cliLocator: host.require('lib/spawn/cliLocator.js'),
      pathSearch: host.require('lib/pathSearch.js'),
    });
  } finally {
    host.restore();
    rmSync(dir, { recursive: true, force: true });
  }
};

const executable = (dir, name = 'stencil') => {
  const path = join(dir, name);
  writeFileSync(path, '#!/bin/sh\nexit 0\n');
  chmodSync(path, 0o755);
  return path;
};

const POSIX_ONLY = { skip: process.platform === 'win32' ? 'POSIX file modes' : false };

test('the chain is the setting, then STENCIL_CLI, then PATH', POSIX_ONLY, () => {
  withLib({}, ({ cliLocator, dir }) => {
    const configured = executable(dir, 'from-setting');
    const onPathDir = join(dir, 'bin');
    mkdirSync(onPathDir);
    const walked = executable(onPathDir);
    const env = { PATH: onPathDir, STENCIL_CLI: executable(dir, 'from-env') };
    assert.equal(cliLocator.locateCli({ configured, env }), configured);
    assert.equal(cliLocator.locateCli({ env }), env.STENCIL_CLI);
    assert.equal(cliLocator.locateCli({ env: { PATH: onPathDir } }), walked);
    assert.equal(cliLocator.locateCli({ env: { PATH: join(dir, 'nowhere') } }), null);
  });
});

test('a setting that names nothing executable is refused, not guessed at', POSIX_ONLY, () => {
  withLib({}, ({ cliLocator, dir }) => {
    const notExecutable = join(dir, 'plain.txt');
    writeFileSync(notExecutable, 'hello\n');
    assert.equal(cliLocator.locateCli({ configured: notExecutable, env: { PATH: '' } }), null);
    assert.equal(cliLocator.locateCli({ configured: dir, env: { PATH: '' } }), null, 'not a file');
  });
});

test('a relative setting resolves against the workspace folder', POSIX_ONLY, () => {
  withLib({}, ({ cliLocator, dir }) => {
    const path = executable(dir, 'rel-stencil');
    assert.equal(cliLocator.resolveConfigured('rel-stencil', dir), path);
    assert.equal(cliLocator.resolveConfigured('rel-stencil', ''), null, 'no base, no guess');
  });
});

test('the PATH walk is memoized per PATH, and a new PATH misses', POSIX_ONLY, () => {
  withLib({}, ({ dir, pathSearch }) => {
    const onPathDir = join(dir, 'bin');
    mkdirSync(onPathDir);
    const path = executable(onPathDir);
    assert.equal(pathSearch.onPath('stencil', { PATH: onPathDir }), path);

    rmSync(path);
    assert.equal(pathSearch.onPath('stencil', { PATH: onPathDir }), path, 'the memo answered');
    assert.equal(pathSearch.onPath('stencil', { PATH: `${onPathDir}:` }), null, 'a new key walks');
    pathSearch.forgetPathWalk();
    assert.equal(pathSearch.onPath('stencil', { PATH: onPathDir }), null, 'forgotten, walked');
  });
});

test('a name with a separator is a path, never a PATH lookup', POSIX_ONLY, () => {
  withLib({}, ({ dir, pathSearch }) => {
    const path = executable(dir);
    assert.equal(pathSearch.onPath(path, { PATH: dir }), path);
    assert.equal(pathSearch.onPath(join(dir, 'absent'), { PATH: dir }), null);
  });
});

test('cliFor reads the setting through the editor, per document', POSIX_ONLY, () => {
  withLib({}, ({ cliLocator, dir, vscode }) => {
    const configured = executable(dir);
    vscode.workspace.getConfiguration = () => ({ get: () => configured });
    assert.equal(cliLocator.cliFor(vscode, makeDocument()), configured);
  });
});

test('the executable-suffix table is frozen', () => {
  withLib({}, ({ pathSearch }) => {
    assert.ok(Object.isFrozen(pathSearch.EXE_SUFFIXES));
    assert.ok(pathSearch.EXE_SUFFIXES.includes(''), 'a bare name is always tried');
  });
});
