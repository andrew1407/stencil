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

// The fourth contributed id, stencil.configureColors, is colors.js's — it spawns no CLI.
test('register hands VS Code every script command', async () => {
  await withHost({}, async ({ calls, commands, host }) => {
    const context = { subscriptions: [] };
    commands.register(context);
    const { COMMANDS } = host.require('lib/ids.js');
    assert.deepEqual([...calls.commands.keys()].sort(),
      [COMMANDS.checkScript, COMMANDS.emitScript, COMMANDS.runPythonScript, COMMANDS.runScript,
        COMMANDS.runScriptOnImage].sort());
    assert.equal(context.subscriptions.length, 5);
  });
});

test('a cancelled Save As spawns nothing — Untitled-1 is a label, not a path', async () => {
  await withHost({}, async ({ calls, commands, vscode }) => {
    const document = makeDocument({
      path: 'Untitled-1', scheme: 'untitled', isDirty: true, save: async () => false,
    });
    vscode.window.activeTextEditor = { document };
    await commands.runScript();
    assert.equal(calls.terminals.length, 0, 'nothing ran against a buffer with no file');
    assert.deepEqual(calls.errors, [], 'a cancelled save is the user saying no');
  });
});

test('a buffer that is not a file on disk is refused with a message', async () => {
  await withHost({}, async ({ calls, commands, vscode }) => {
    const document = makeDocument({ path: 'Untitled-1', scheme: 'untitled' });
    vscode.window.activeTextEditor = { document };
    await commands.runScript();
    assert.deepEqual(calls.errors, ['Save the script to a file first']);
    assert.equal(calls.terminals.length, 0);
  });
});

test('a saved buffer runs, and the save answer is only consulted when dirty', async () => {
  await withHost({}, async ({ calls, commands, dir, vscode }) => {
    let saves = 0;
    const path = join(dir, 'demo.stc');
    writeFileSync(path, '@source a.png:\n');
    vscode.window.activeTextEditor = {
      document: makeDocument({ path, save: async () => { saves += 1; return false; } }),
    };
    await commands.runScript();
    assert.equal(saves, 0, 'a clean buffer is not saved');
    assert.equal(calls.terminals.length, 1);
  });
});

test('the handler table and the id table are frozen', async () => {
  await withHost({}, async ({ commands, host }) => {
    assert.ok(Object.isFrozen(commands.HANDLERS));
    const ids = host.require('lib/ids.js');
    assert.ok(Object.isFrozen(ids.COMMANDS) && Object.isFrozen(ids.SETTINGS));
    const tokens = host.require('semanticTokens.js');
    assert.ok(Object.isFrozen(tokens.TOKEN_TYPES) && Object.isFrozen(tokens.KIND_TYPE));
  });
});

test('emit hands the CLI the picked extension, and only that', async () => {
  await withHost({ quickPick: [{ label: '.pystc' }] }, async ({ calls, commands, dir, vscode }) => {
    const path = openScript(vscode, dir);
    await commands.emitScript();
    const line = calls.terminals[0].sent.at(-1);
    assert.ok(line.includes('--script '), 'the script it reads');
    assert.ok(line.includes('--script-emit '), 'the flag that writes');
    assert.ok(line.endsWith(path.replace(/\.stc$/, '.pystc')), 'beside the script, under its stem');
    assert.deepEqual(calls.picks[0].items.map((i) => i.label), ['.pystc', '.py', '.stcjs', '.js']);
  });
});

test('a cancelled emit pick spawns nothing', async () => {
  await withHost({ quickPick: [undefined] }, async ({ calls, commands, dir, vscode }) => {
    openScript(vscode, dir);
    await commands.emitScript();
    assert.equal(calls.terminals.length, 0);
  });
});

test('a .pystc runs on the interpreter, not on the CLI', async () => {
  await withHost({ settings: { 'stencil.pythonPath': '' } }, async ({ calls, commands, dir, vscode }) => {
    const path = join(dir, 'shots.pystc');
    writeFileSync(path, '# @use stencil\n');
    vscode.window.activeTextEditor = { document: makeDocument({ path, languageId: 'stencil-py' }) };
    await commands.runPythonScript();
    const line = calls.terminals[0].sent.at(-1);
    assert.ok(/python3?/.test(line), 'the interpreter leads');
    assert.ok(line.endsWith(path), 'and the script is its only argument');
    assert.ok(!line.includes('--script'), 'no CLI flag reaches it');
  });
});

test('running Python over a .stc buffer says which file it wanted', async () => {
  await withHost({}, async ({ calls, commands, dir, vscode }) => {
    openScript(vscode, dir);
    await commands.runPythonScript();
    assert.equal(calls.terminals.length, 0);
    assert.deepEqual(calls.errors, ['Open a .pystc script first']);
  });
});
