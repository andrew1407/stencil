// The two switches over the check: stencil.checkOnSave decides whether a saved file reaches the
// CLI, stencil.checkOnType whether typing checks at all. Neither may leave a buffer unchecked.
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';

import { installVscodeStub, makeDocument, makeVscode } from './helpers/vscodeStub.js';

const POSIX_ONLY = { skip: process.platform === 'win32' ? 'POSIX stub script' : false };
const TEXT = '@source a.png:\n    @crp 10%\n';

const withHost = (settings, body) => {
  const { vscode, calls } = makeVscode({ settings });
  const host = installVscodeStub(vscode);
  try {
    return body({ calls, diagnostics: host.require('diagnostics.js') });
  } finally {
    host.restore();
  }
};

// A CLI answering with a code no parser can produce, so its absence from the answer is visible.
const withCli = async (settings, body) => {
  const dir = mkdtempSync(join(tmpdir(), 'stencil-vsce-'));
  const script = join(dir, 'demo.stc');
  const cli = join(dir, 'stencil');
  writeFileSync(script, TEXT);
  writeFileSync(cli, '#!/bin/sh\necho "$2:9:4: error: from the CLI [E_FROM_CLI]"\nexit 1\n', { mode: 0o755 });
  try {
    await withHost({ 'stencil.cliPath': cli, ...settings },
      (booted) => body({ ...booted, document: makeDocument({ path: script, text: TEXT }) }));
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
};

test('checkOnSave on: a saved file is answered by the CLI', POSIX_ONLY, async () => {
  await withCli({}, async ({ diagnostics, document }) => {
    const [found] = await diagnostics.collect(document, { saved: true });
    assert.equal(found.code, 'E_FROM_CLI');
  });
});

test('checkOnSave off: nothing is spawned, and the copies answer instead', POSIX_ONLY, async () => {
  await withCli({ 'stencil.checkOnSave': false }, async ({ diagnostics, document }) => {
    const [found] = await diagnostics.collect(document, { saved: true });
    assert.equal(found.code, 'E_UNKNOWN_DIRECTIVE', 'a saved file is never left unchecked');
  });
});

test('checkOnType off: typing schedules no check at all', async () => {
  const settings = { 'stencil.cliPath': '/nowhere/stencil', 'stencil.checkOnType': false };
  await withHost(settings, async ({ calls, diagnostics }) => {
    diagnostics.register({ subscriptions: [] });
    calls.events.change[0]({ document: makeDocument({ text: TEXT }) });
    await delay(diagnostics.DEBOUNCE_MS * 2);
    assert.equal(calls.collections[0].entries.size, 0, 'nothing was checked while typing');
  });
});
