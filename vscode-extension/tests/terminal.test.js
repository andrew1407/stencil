// The composed command line, per shell family. VS Code's terminal is whichever shell the user
// picked, and the quoting that keeps a path in one argument differs in all three.
import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';

import { installVscodeStub, makeVscode } from './helpers/vscodeStub.js';

const withLib = (shell, body) => {
  const { vscode, calls } = makeVscode({ shell });
  const host = installVscodeStub(vscode);
  try {
    return body({
      calls, vscode,
      shellQuote: host.require('lib/shellQuote.js'),
      terminal: host.require('lib/terminal.js'),
    });
  } finally {
    host.restore();
  }
};

const SHELLS = {
  '/bin/zsh': 'posix',
  '/usr/local/bin/fish': 'posix',
  'C:\\Program Files\\Git\\bin\\bash.exe': 'posix',
  'C:\\WINDOWS\\System32\\cmd.exe': 'cmd',
  'cmd.exe': 'cmd',
  'C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe': 'powershell',
  'C:\\Program Files\\PowerShell\\7\\pwsh.exe': 'powershell',
};

test('vscode.env.shell picks the family; an unknown one is POSIX', () => {
  withLib('/bin/sh', ({ shellQuote }) => {
    for (const [shell, kind] of Object.entries(SHELLS)) {
      assert.equal(shellQuote.shellKind(shell), kind, shell);
    }
    assert.equal(shellQuote.shellKind('/opt/homebrew/bin/nu'), 'posix');
    assert.equal(shellQuote.shellFor('nonsense'), shellQuote.SHELLS.posix, 'never undefined');
  });
});

test('a path with a space stays one argument in every shell', () => {
  withLib('/bin/sh', ({ terminal }) => {
    const path = 'C:\\a b\\x.stc';
    assert.equal(terminal.quoteArg(path, 'posix'), "'C:\\a b\\x.stc'");
    assert.equal(terminal.quoteArg(path, 'powershell'), "'C:\\a b\\x.stc'");
    assert.equal(terminal.quoteArg(path, 'cmd'), '"C:\\a b\\x.stc"');
  });
});

test('a single quote is escaped the way each shell escapes it', () => {
  withLib('/bin/sh', ({ terminal }) => {
    const name = "it's.stc";
    assert.equal(terminal.quoteArg(name, 'posix'), "'it'\\''s.stc'", "closed, escaped, reopened");
    assert.equal(terminal.quoteArg(name, 'powershell'), "'it''s.stc'", 'doubled, not backslashed');
    assert.equal(terminal.quoteArg(name, 'cmd'), '"it\'s.stc"', 'no meaning inside cmd quotes');
  });
});

test('each shell quotes what only it treats as special', () => {
  withLib('/bin/sh', ({ terminal }) => {
    assert.equal(terminal.quoteArg('100%.stc', 'cmd'), '"100%.stc"', 'cmd expands %NAME%');
    assert.equal(terminal.quoteArg('a,b.stc', 'cmd'), '"a,b.stc"', 'cmd splits on a comma');
    assert.equal(terminal.quoteArg('@a.stc', 'powershell'), "'@a.stc'", 'a leading @ splats');
    assert.equal(terminal.quoteArg('100%.stc', 'posix'), '100%.stc', 'a POSIX shell is fine');
    assert.equal(terminal.quoteArg('a"b & del *', 'cmd'), '"ab & del *"', 'no way out of cmd quotes');
  });
});

test('an empty argument survives as a quoted pair, not as nothing', () => {
  withLib('/bin/sh', ({ terminal }) => {
    assert.equal(terminal.quoteArg('', 'posix'), "''");
    assert.equal(terminal.quoteArg(undefined, 'powershell'), "''");
    assert.equal(terminal.quoteArg(null, 'cmd'), '""');
  });
});

test('PowerShell runs a quoted command word with &; the other shells do not', () => {
  withLib('/bin/sh', ({ terminal }) => {
    const line = (kind) => terminal.commandLine('/opt/a b/stencil', ['--script', 'x.stc'], kind);
    assert.equal(line('powershell'), "& '/opt/a b/stencil' --script x.stc");
    assert.equal(line('posix'), "'/opt/a b/stencil' --script x.stc");
    assert.equal(line('cmd'), '"/opt/a b/stencil" --script x.stc');
    assert.equal(terminal.commandLine('stencil', ['x.stc'], 'powershell'), 'stencil x.stc',
      'a bare word needs no &');
  });
});

test('cmd.exe changes drive with cd /d; the others just cd', () => {
  const run = (shell) => withLib(shell, ({ calls, terminal, vscode }) => {
    terminal.runInTerminal(vscode, { cli: 'stencil', args: ['--script', 'a.stc'], cwd: 'D:\\w s' });
    return calls.terminals[0].sent;
  });
  assert.deepEqual(run('C:\\WINDOWS\\System32\\cmd.exe'),
    ['cd /d "D:\\w s"', 'stencil --script a.stc']);
  assert.deepEqual(run('C:\\Program Files\\PowerShell\\7\\pwsh.exe'),
    ["cd 'D:\\w s'", 'stencil --script a.stc']);
  assert.deepEqual(run('/bin/zsh'), ["cd 'D:\\w s'", 'stencil --script a.stc']);
});

test('the POSIX line round-trips through a real shell, argument for argument',
  { skip: process.platform === 'win32' ? 'POSIX shell round-trip' : false }, () => {
    withLib('/bin/sh', ({ terminal }) => {
      const nasty = "a b'; rm -rf ~; #.stc";
      const echoed = spawnSync('/bin/sh', ['-c', terminal.commandLine('printf', ['%s', nasty])],
        { encoding: 'utf8' });
      assert.equal(echoed.stdout, nasty);
      assert.equal(echoed.status, 0);
    });
  });
