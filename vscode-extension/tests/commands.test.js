// The three commands, the reused terminal, and the quoting rule: nothing that came out of a
// document or a picker may reach the shell unquoted.
import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { chmodSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { installVscodeStub, makeDocument, makeVscode } from './helpers/vscodeStub.js';

const fakeCli = (dir) => {
  const cli = join(dir, 'stencil');
  writeFileSync(cli, '#!/bin/sh\nexit 0\n');
  chmodSync(cli, 0o755);
  return cli;
};

const withHost = async (options, body) => {
  const dir = mkdtempSync(join(tmpdir(), 'stencil-vsce-'));
  const { vscode, calls } = makeVscode({
    ...options, settings: { 'stencil.cliPath': fakeCli(dir), ...(options.settings ?? {}) },
  });
  const host = installVscodeStub(vscode);
  try {
    return await body({ calls, dir, host, vscode, commands: host.require('commands.js') });
  } finally {
    host.restore();
    rmSync(dir, { recursive: true, force: true });
  }
};

const openScript = (vscode, dir, name = 'demo.stc') => {
  const path = join(dir, name);
  writeFileSync(path, '@source a.png:\n');
  const document = makeDocument({ path });
  vscode.window.activeTextEditor = { document };
  return path;
};

test('run sends `--script` to the Stencil terminal, in the script\'s directory', async () => {
  await withHost({}, async ({ calls, commands, dir, vscode }) => {
    const path = openScript(vscode, dir);
    await commands.runScript();
    const [terminal] = calls.terminals;
    assert.equal(terminal.name, 'Stencil');
    assert.equal(terminal.cwd, dir);
    assert.ok(terminal.sent.at(-1).includes('--script '), 'the run flag');
    assert.ok(terminal.sent.at(-1).endsWith(path), 'the script path');
  });
});

test('check sends `--script-check`, and the terminal is reused', async () => {
  await withHost({}, async ({ calls, commands, dir, vscode }) => {
    openScript(vscode, dir);
    await commands.runScript();
    await commands.checkScript();
    assert.equal(calls.terminals.length, 1, 'one Stencil terminal, reused');
    assert.ok(calls.terminals[0].sent.at(-1).includes('--script-check'));
  });
});

test('run-on-image puts the picked file behind -i, ahead of the script', async () => {
  const image = join(tmpdir(), 'pic ture.png');
  await withHost({ openDialog: [{ fsPath: image }] }, async ({ calls, commands, dir, vscode }) => {
    openScript(vscode, dir);
    await commands.runScriptOnImage();
    const sent = calls.terminals[0].sent.at(-1);
    assert.match(sent, /-i '.*pic ture\.png' --script /, 'the image is quoted and comes first');
  });
});

test('run-on-image with no pick sends nothing', async () => {
  await withHost({ openDialog: [] }, async ({ calls, commands, dir, vscode }) => {
    openScript(vscode, dir);
    await commands.runScriptOnImage();
    assert.equal(calls.terminals.length, 0);
  });
});

test('a path holding a quote, a space or a semicolon cannot break out of its argument',
  { skip: process.platform === 'win32' ? 'POSIX shell round-trip' : false }, async () => {
    const nasty = "a b'; rm -rf ~; #.stc";
    await withHost({}, async ({ calls, commands, dir, host, vscode }) => {
      openScript(vscode, dir, nasty);
      await commands.runScript();
      assert.match(calls.terminals[0].sent.at(-1), /--script '.*'$/,
        'the whole path is one quoted argument');

      // The real proof: a shell reading the composed line hands the argument back whole.
      const { commandLine } = host.require('lib/terminal.js');
      const echoed = spawnSync('/bin/sh', ['-c', commandLine('printf', ['%s', nasty])],
        { encoding: 'utf8' });
      assert.equal(echoed.stdout, nasty);
      assert.equal(echoed.status, 0);
    });
  });

test('no .stc buffer and no CLI each refuse with a message instead of spawning', async () => {
  await withHost({}, async ({ calls, commands, vscode }) => {
    vscode.window.activeTextEditor = { document: makeDocument({ languageId: 'plaintext' }) };
    await commands.runScript();
    assert.deepEqual(calls.errors, ['Open a .stc script first']);
    assert.equal(calls.terminals.length, 0);
  });

  await withHost({ settings: { 'stencil.cliPath': '/nowhere/stencil' } },
    async ({ calls, commands, dir, vscode }) => {
      openScript(vscode, dir);
      const before = { ...process.env };
      delete process.env.STENCIL_CLI;
      process.env.PATH = '/nonexistent-for-this-test';
      try {
        await commands.runScript();
      } finally {
        process.env.PATH = before.PATH;
        if (before.STENCIL_CLI) process.env.STENCIL_CLI = before.STENCIL_CLI;
      }
      assert.deepEqual(calls.errors, ['Stencil CLI not found — set stencil.cliPath']);
      assert.equal(calls.terminals.length, 0);
    });
});

test('register hands VS Code exactly the three contributed ids', async () => {
  await withHost({}, async ({ calls, commands, host }) => {
    const context = { subscriptions: [] };
    commands.register(context);
    const { COMMANDS } = host.require('lib/ids.js');
    assert.deepEqual([...calls.commands.keys()].sort(), Object.values(COMMANDS).sort());
    assert.equal(context.subscriptions.length, 3);
  });
});
