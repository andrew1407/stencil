// Finding the interpreter a .pystc runs on: the setting, then STENCIL_PYTHON, then python3
// and python on PATH — and never a path read out of the document.
import test from 'node:test';
import assert from 'node:assert/strict';
import { chmodSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { locatePython } from '../../../src/lib/spawn/pythonLocator.js';

const POSIX_ONLY = { skip: process.platform === 'win32' ? 'POSIX file modes' : false };

const withDir = (body) => {
  const dir = mkdtempSync(join(tmpdir(), 'stencil-vsce-py-'));
  try {
    return body(dir);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
};

const executable = (dir, name) => {
  const path = join(dir, name);
  writeFileSync(path, '#!/bin/sh\nexit 0\n');
  chmodSync(path, 0o755);
  return path;
};

test('the chain is the setting, then STENCIL_PYTHON, then PATH', POSIX_ONLY, () => {
  withDir((dir) => {
    const configured = executable(dir, 'from-setting');
    const fromEnv = executable(dir, 'from-env');
    const bin = join(dir, 'bin');
    mkdirSync(bin);
    const onPath = executable(bin, 'python3');
    const env = { PATH: bin, STENCIL_PYTHON: fromEnv };

    assert.equal(locatePython({ configured, env }), configured);
    assert.equal(locatePython({ env }), fromEnv);
    assert.equal(locatePython({ env: { PATH: bin } }), onPath);
  });
});

test('python answers where python3 does not, and nothing when neither does', POSIX_ONLY, () => {
  withDir((dir) => {
    const bin = join(dir, 'bin');
    mkdirSync(bin);
    const python = executable(bin, 'python');
    assert.equal(locatePython({ env: { PATH: bin } }), python);
    assert.equal(locatePython({ env: { PATH: join(dir, 'empty') } }), null);
  });
});

test('a configured path that names no executable is not silently replaced', POSIX_ONLY, () => {
  withDir((dir) => {
    const bin = join(dir, 'bin');
    mkdirSync(bin);
    executable(bin, 'python3');
    assert.equal(locatePython({ configured: join(dir, 'missing'), env: { PATH: bin } }),
      join(bin, 'python3'));
  });
});

test('a relative setting resolves against the workspace folder, not the cwd', POSIX_ONLY, () => {
  withDir((dir) => {
    const bin = join(dir, 'bin');
    mkdirSync(bin);
    executable(bin, 'py-here');
    assert.equal(locatePython({ configured: 'bin/py-here', baseDir: dir, env: { PATH: '' } }),
      join(bin, 'py-here'));
  });
});
