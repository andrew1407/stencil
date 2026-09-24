// Both diagnostic sources land on the same vscode.Diagnostic: the CLI's `--script-check`
// lines and the in-process parser copies.
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';

import { installVscodeStub, makeDocument, makeVscode } from '../../helpers/vscodeStub.js';

const boot = async (options) => {
  const { vscode, calls } = makeVscode(options);
  const host = installVscodeStub(vscode);
  return { calls, host, diagnostics: await host.import('diagnostics.js') };
};

const withHost = async (options, body) => {
  const booted = await boot(options);
  try { return await body(booted); } finally { booted.host.restore(); }
};

test('the check grammar reads one CLI line into a diagnostic entry', async () => {
  await withHost({}, ({ diagnostics }) => {
    const [entry] = diagnostics.parseCheckOutput(
      "a.stc:3:5: error: unknown directive '@crp' [E_UNKNOWN_DIRECTIVE]\n");
    assert.deepEqual(entry, {
      line: 3, col: 5, len: 1, severity: 'error',
      message: "unknown directive '@crp'", code: 'E_UNKNOWN_DIRECTIVE',
    });
  });
});

test('a Windows path keeps its drive letter and a warning stays a warning', async () => {
  await withHost({}, ({ diagnostics }) => {
    const found = diagnostics.parseCheckOutput([
      'C:\\work\\a.stc:1:1: warning: the block has no operations [W_EMPTY_BLOCK]',
      'not a diagnostic line',
      '',
    ].join('\n'));
    assert.equal(found.length, 1);
    assert.equal(found[0].severity, 'warning');
    assert.equal(found[0].code, 'W_EMPTY_BLOCK');
  });
});

test('a diagnostic with no code still parses', async () => {
  await withHost({}, ({ diagnostics }) => {
    const [entry] = diagnostics.parseCheckOutput('a.stc:2:1: error: something went wrong');
    assert.equal(entry.code, '');
    assert.equal(entry.message, 'something went wrong');
  });
});

test('an entry becomes a 0-based, never-empty range', async () => {
  await withHost({}, ({ diagnostics }) => {
    const d = diagnostics.toDiagnostic({ line: 2, col: 13, len: 7, severity: 'error', message: 'nope', code: 'E_X' });
    assert.equal(d.range.start.line, 1);
    assert.equal(d.range.start.character, 12);
    assert.equal(d.range.end.character, 19);
    assert.equal(d.severity, 0);
    assert.equal(d.source, 'stencil');
    assert.equal(d.code, 'E_X');

    const zero = diagnostics.toDiagnostic({ line: 1, col: 1, len: 0, severity: 'warning', message: 'thin' });
    assert.equal(zero.range.end.character, 1, 'a zero-length span would draw nothing');
    assert.equal(zero.severity, 1);
  });
});

test('an unsaved buffer is checked in process, by the parser copies', async () => {
  await withHost({}, async ({ diagnostics }) => {
    const document = makeDocument({ text: '@source a.png:\n    @crp 10%\n' });
    const found = await diagnostics.collect(document, { saved: false });
    assert.equal(found.length, 1);
    assert.equal(found[0].code, 'E_UNKNOWN_DIRECTIVE');
    assert.equal(found[0].line, 2);
  });
});

test('a clean script yields no diagnostics', async () => {
  await withHost({}, async ({ diagnostics }) => {
    const document = makeDocument({ text: '@source a.png:\n    @crop 10%\n    @save o.png\n' });
    assert.deepEqual(await diagnostics.collect(document, { saved: false }), []);
  });
});

test('a saved buffer with no CLI configured falls back to the parser copies', async () => {
  await withHost({ settings: { 'stencil.cliPath': '/nowhere/stencil' } }, async ({ diagnostics }) => {
    const document = makeDocument({ text: '@source a.png:\n    @crp 10%\n' });
    const found = await diagnostics.collect(document, { saved: true });
    assert.equal(found[0].code, 'E_UNKNOWN_DIRECTIVE');
  });
});

test('register wires the collection and the four document events', async () => {
  await withHost({}, ({ calls, diagnostics }) => {
    const context = { subscriptions: [] };
    const collection = diagnostics.register(context);
    assert.equal(collection.name, 'stencil-script');
    assert.deepEqual(Object.keys(calls.events).sort(), ['change', 'close', 'open', 'save']);
    assert.ok(context.subscriptions.includes(collection));
  });
});

test('a document of another language is left alone', async () => {
  await withHost({}, async ({ calls, diagnostics }) => {
    diagnostics.register({ subscriptions: [] });
    const [onSave] = calls.events.save;
    await onSave(makeDocument({ languageId: 'plaintext', text: '@crp' }));
    assert.equal(calls.collections[0].entries.size, 0);
  });
});

test('a saved buffer with a working CLI takes the CLI\'s answer, verbatim',
  { skip: process.platform === 'win32' ? 'POSIX stub script' : false }, async () => {
    const dir = mkdtempSync(join(tmpdir(), 'stencil-vsce-'));
    const cli = join(dir, 'stencil');
    const script = join(dir, 'demo.stc');
    writeFileSync(script, '@source a.png:\n');
    writeFileSync(cli, '#!/bin/sh\necho "$2:9:4: error: from the CLI [E_FROM_CLI]"\n', { mode: 0o755 });
    try {
      await withHost({ settings: { 'stencil.cliPath': cli } }, async ({ diagnostics }) => {
        const found = await diagnostics.collect(makeDocument({ path: script, text: '' }), { saved: true });
        assert.deepEqual(found, [{
          line: 9, col: 4, len: 1, severity: 'error', message: 'from the CLI', code: 'E_FROM_CLI',
        }]);
      });
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });

const fakeCli = (dir, body) => {
  const cli = join(dir, 'stencil');
  writeFileSync(cli, `#!/bin/sh\n${body}\n`, { mode: 0o755 });
  return cli;
};

const withCli = async (body, run) => {
  const dir = mkdtempSync(join(tmpdir(), 'stencil-vsce-'));
  const script = join(dir, 'demo.stc');
  writeFileSync(script, '@source a.png:\n    @crp 10%\n');
  const cli = fakeCli(dir, body);
  try {
    await withHost({ settings: { 'stencil.cliPath': cli } }, async (booted) => {
      await run({ ...booted, cli, script });
    });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
};

const POSIX_ONLY = { skip: process.platform === 'win32' ? 'POSIX stub script' : false };

test('a CLI that says nothing parsable falls back to the parser copies', POSIX_ONLY, async () => {
  // Exit 2 is an argv refusal: the wrong binary, or a version that never heard of the flag.
  await withCli('echo "stencil: unknown option" >&2; exit 2', async ({ diagnostics, script }) => {
    const document = makeDocument({ path: script, text: '@source a.png:\n    @crp 10%\n' });
    const found = await diagnostics.collect(document, { saved: true });
    assert.equal(found.length, 1, 'the copies answered');
    assert.equal(found[0].code, 'E_UNKNOWN_DIRECTIVE');
  });
});

test('a failing CLI that printed no diagnostic falls back too', POSIX_ONLY, async () => {
  await withCli('exit 1', async ({ diagnostics, script }) => {
    const document = makeDocument({ path: script, text: '@source a.png:\n    @crp 10%\n' });
    const found = await diagnostics.collect(document, { saved: true });
    assert.equal(found[0].code, 'E_UNKNOWN_DIRECTIVE', 'squiggles are never silently cleared');
  });
});

test('a clean exit with no output IS an answer: no diagnostics', POSIX_ONLY, async () => {
  await withCli('exit 0', async ({ diagnostics, script }) => {
    const document = makeDocument({ path: script, text: '@source a.png:\n    @crp 10%\n' });
    assert.deepEqual(await diagnostics.collect(document, { saved: true }), []);
  });
});

test('typing is debounced: a burst of changes paints once', async () => {
  await withHost({ settings: { 'stencil.cliPath': '/nowhere/stencil' } },
    async ({ calls, diagnostics }) => {
      diagnostics.register({ subscriptions: [] });
      const [onChange] = calls.events.change;
      const document = makeDocument({ text: '@source a.png:\n    @crp 10%\n', version: 1 });
      for (let i = 0; i < 5; i += 1) onChange({ document });
      assert.equal(calls.collections[0].entries.size, 0, 'nothing while the burst lasts');
      await delay(diagnostics.DEBOUNCE_MS * 2);
      assert.equal(calls.collections[0].entries.get(document.uri.fsPath).length, 1);
    });
});

test('a result the next keystroke outdated is dropped, not painted', async () => {
  await withHost({ settings: { 'stencil.cliPath': '/nowhere/stencil' } },
    async ({ calls, diagnostics }) => {
      diagnostics.register({ subscriptions: [] });
      const [onSave] = calls.events.save;
      const document = makeDocument({ text: '@source a.png:\n    @crp 10%\n', version: 1 });
      const painting = onSave(document);
      document.version = 2;
      await painting;
      assert.equal(calls.collections[0].entries.size, 0, 'a stale parse never overwrites');
    });
});

test('closing a document drops its squiggles and its pending check', async () => {
  await withHost({ settings: { 'stencil.cliPath': '/nowhere/stencil' } },
    async ({ calls, diagnostics }) => {
      diagnostics.register({ subscriptions: [] });
      const [onChange] = calls.events.change;
      const [onClose] = calls.events.close;
      const document = makeDocument({ text: '@source a.png:\n    @crp 10%\n', version: 1 });
      onChange({ document });
      onClose(document);
      await delay(diagnostics.DEBOUNCE_MS * 2);
      assert.equal(calls.collections[0].entries.size, 0, 'the debounced check was cancelled');
    });
});

test('a CLI that never answers is killed, and the copies take over', POSIX_ONLY, async () => {
  await withCli('cat', async ({ cli, host, script }) => {
    const { CHECK_TIMEOUT_MS, runCheck } = await host.import('lib/scriptCheck.js');
    assert.ok(CHECK_TIMEOUT_MS > 0, 'the wait on the child is bounded');
    assert.equal(await runCheck(cli, script, 150), null, 'a kill is no answer, not an empty one');
  });
});
