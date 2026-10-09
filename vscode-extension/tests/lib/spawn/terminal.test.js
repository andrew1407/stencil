// The composed command line, per shell family. VS Code's terminal is whichever shell the user
// picked, and the quoting that keeps a path in one argument differs in all three.
import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { existsSync, mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { installVscodeStub, makeVscode } from '../../helpers/vscodeStub.js';

const withLib = async (shell, body) => {
  const { vscode, calls } = makeVscode({ shell });
  const host = installVscodeStub(vscode);
  try {
    return await body({
      calls, vscode,
      shellQuote: await host.import('lib/spawn/shellQuote.js'),
      terminal: await host.import('lib/spawn/terminal.js'),
    });
  } finally {
    host.restore();
  }
};

const SHELLS = {
  '/bin/zsh': 'posix',
  '/usr/local/bin/fish': 'fish',
  'C:\\Program Files\\Git\\bin\\bash.exe': 'posix',
  'C:\\WINDOWS\\System32\\cmd.exe': 'cmd',
  'cmd.exe': 'cmd',
  'C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe': 'powershell',
  'C:\\Program Files\\PowerShell\\7\\pwsh.exe': 'powershell',
};

test('vscode.env.shell picks the family; an unknown one is POSIX', async () => {
  await withLib('/bin/sh', ({ shellQuote }) => {
    for (const [shell, kind] of Object.entries(SHELLS)) {
      assert.equal(shellQuote.shellKind(shell), kind, shell);
    }
    assert.equal(shellQuote.shellKind('/opt/homebrew/bin/nu'), 'posix');
    assert.equal(shellQuote.shellFor('nonsense'), shellQuote.SHELLS.posix, 'never undefined');
  });
});

test('a path with a space stays one argument in every shell', async () => {
  await withLib('/bin/sh', ({ terminal }) => {
    const path = 'C:\\a b\\x.stc';
    assert.equal(terminal.quoteArg(path, 'posix'), "'C:\\a b\\x.stc'");
    assert.equal(terminal.quoteArg(path, 'powershell'), "'C:\\a b\\x.stc'");
    assert.equal(terminal.quoteArg(path, 'cmd'), '"C:\\a b\\x.stc"');
  });
});

test('a single quote is escaped the way each shell escapes it', async () => {
  await withLib('/bin/sh', ({ terminal }) => {
    const name = "it's.stc";
    assert.equal(terminal.quoteArg(name, 'posix'), "'it'\\''s.stc'", "closed, escaped, reopened");
    assert.equal(terminal.quoteArg(name, 'powershell'), "'it''s.stc'", 'doubled, not backslashed');
    assert.equal(terminal.quoteArg(name, 'cmd'), '"it\'s.stc"', 'no meaning inside cmd quotes');
  });
});

test('each shell quotes what only it treats as special', async () => {
  await withLib('/bin/sh', ({ terminal }) => {
    assert.equal(terminal.quoteArg('100%.stc', 'cmd'), '"100%.stc"', 'cmd expands %NAME%');
    assert.equal(terminal.quoteArg('a,b.stc', 'cmd'), '"a,b.stc"', 'cmd splits on a comma');
    assert.equal(terminal.quoteArg('@a.stc', 'powershell'), "'@a.stc'", 'a leading @ splats');
    assert.equal(terminal.quoteArg('100%.stc', 'posix'), '100%.stc', 'a POSIX shell is fine');
    assert.equal(terminal.quoteArg('a"b & del *', 'cmd'), '"ab & del *"', 'no way out of cmd quotes');
  });
});

test('an empty argument survives as a quoted pair, not as nothing', async () => {
  await withLib('/bin/sh', ({ terminal }) => {
    assert.equal(terminal.quoteArg('', 'posix'), "''");
    assert.equal(terminal.quoteArg(undefined, 'powershell'), "''");
    assert.equal(terminal.quoteArg(null, 'cmd'), '""');
  });
});

test('PowerShell runs a quoted command word with &; the other shells do not', async () => {
  await withLib('/bin/sh', ({ terminal }) => {
    const line = (kind) => terminal.commandLine('/opt/a b/stencil', ['--script', 'x.stc'], kind);
    assert.equal(line('powershell'), "& '/opt/a b/stencil' --script x.stc");
    assert.equal(line('posix'), "'/opt/a b/stencil' --script x.stc");
    assert.equal(line('cmd'), '"/opt/a b/stencil" --script x.stc');
    assert.equal(terminal.commandLine('stencil', ['x.stc'], 'powershell'), 'stencil x.stc',
      'a bare word needs no &');
  });
});

test('cmd.exe changes drive with cd /d; the others just cd', async () => {
  const run = (shell) => withLib(shell, ({ calls, terminal, vscode }) => {
    terminal.runInTerminal(vscode, { cli: 'stencil', args: ['--script', 'a.stc'], cwd: 'D:\\w s' });
    return calls.terminals[0].sent;
  });
  assert.deepEqual(await run('C:\\WINDOWS\\System32\\cmd.exe'),
    ['cd /d "D:\\w s"', 'stencil --script a.stc']);
  assert.deepEqual(await run('C:\\Program Files\\PowerShell\\7\\pwsh.exe'),
    ["cd 'D:\\w s'", 'stencil --script a.stc']);
  assert.deepEqual(await run('/bin/zsh'), ["cd 'D:\\w s'", 'stencil --script a.stc']);
});

test('the POSIX line round-trips through a real shell, argument for argument',
  { skip: process.platform === 'win32' ? 'POSIX shell round-trip' : false }, async () => {
    await withLib('/bin/sh', ({ terminal }) => {
      const nasty = "a b'; rm -rf ~; #.stc";
      const echoed = spawnSync('/bin/sh', ['-c', terminal.commandLine('printf', ['%s', nasty])],
        { encoding: 'utf8' });
      assert.equal(echoed.stdout, nasty);
      assert.equal(echoed.status, 0);
    });
  });

// A directory name that escapes a POSIX-quoted fish argument: fish reads \' inside quotes as '.
const HOSTILE = "x\\';touch pwned;#";

// fish's single-quote rules: \\ and \' are escapes, any other backslash is literal.
const fishUnquote = (word) => {
  assert.match(word, /^'.*'$/s);
  let out = '';
  for (let k = 1; k < word.length - 1; k += 1) {
    const c = word[k], next = word[k + 1];
    if (c === "'") assert.fail(`an unescaped quote ends the word at ${k}: ${word}`);
    if (c === '\\' && (next === '\\' || next === "'")) { out += next; k += 1; } else out += c;
  }
  return out;
};

test('the hostile directory name stays one literal argument in every shell', async () => {
  await withLib('/bin/sh', ({ terminal }) => {
    assert.equal(terminal.quoteArg(HOSTILE, 'fish'), "'x\\\\\\';touch pwned;#'");
    assert.equal(fishUnquote(terminal.quoteArg(HOSTILE, 'fish')), HOSTILE);
    assert.equal(terminal.quoteArg(HOSTILE, 'posix'), "'x\\'\\'';touch pwned;#'");
    assert.equal(terminal.quoteArg(HOSTILE, 'powershell'), "'x\\'';touch pwned;#'");
    assert.equal(terminal.quoteArg(HOSTILE, 'cmd'), '"x\\\';touch pwned;#"');
    assert.equal(terminal.quoteArg('%self', 'fish'), "'%self'", 'fish expands a leading %');
  });
});

for (const shell of ['/bin/bash', '/bin/zsh', '/usr/bin/fish', '/opt/homebrew/bin/fish', '/usr/local/bin/pwsh']) {
  test(`${shell} gets the hostile name back as one argument and runs nothing`,
    { skip: process.platform === 'win32' || !existsSync(shell) ? `${shell} not installed` : false }, async () => {
      await withLib(shell, ({ shellQuote, terminal }) => {
        const kind = shellQuote.shellKind(shell);
        const cwd = mkdtempSync(join(tmpdir(), 'stencil-quote-'));
        const flag = kind === 'powershell' ? ['-NoProfile', '-Command'] : ['-c'];
        const echoed = spawnSync(shell, [...flag, terminal.commandLine('printf', ['%s', HOSTILE], kind)],
          { encoding: 'utf8', cwd });
        assert.equal(echoed.stdout, HOSTILE);
        assert.equal(existsSync(join(cwd, 'pwned')), false, 'touch never ran');
      });
    });
}
