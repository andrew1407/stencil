// A diagnostic raised inside an expanded template points back at its `@use stencil` call as
// relatedInformation, from either source: the parser copies carry the span, and a
// `--script-check` line borrows it from the copies' own diagnostic with the same span.
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { installVscodeStub, makeContext, makeDocument, makeVscode } from '../../helpers/vscodeStub.js';

// The body's @line has one point; the call on line 5 sits after an é, 2 bytes in 1 unit.
const TEXT = '@stencil bad:\n  @line (0,0)\n\n@source a.png:\n  @save "é.png"; @use stencil bad\n';
const CALL = { line: 5, col: 19, len: 4 };

class Location {
  constructor(uri, range) { this.uri = uri; this.range = range; }
}
class DiagnosticRelatedInformation {
  constructor(location, message) { this.location = location; this.message = message; }
}

const withHost = async (settings, body) => {
  const { vscode, calls } = makeVscode({ settings });
  Object.assign(vscode, { Location, DiagnosticRelatedInformation });
  const host = installVscodeStub(vscode);
  try {
    return await body({ calls, diagnostics: await host.import('diagnostics.js') });
  } finally {
    host.restore();
  }
};
const NO_CLI = { 'stencil.cliPath': '/nowhere/stencil' };

test('the parser copies carry the call site of a template-body diagnostic', async () => {
  await withHost(NO_CLI, async ({ diagnostics }) => {
    const found = await diagnostics.collect(makeDocument({ text: TEXT }), { saved: false });
    const body = found.find((d) => d.line === 2);
    assert.deepEqual(body.related, CALL);
    assert.ok(body.message.endsWith('(from the @use stencil at 5:19)'), 'the text is unchanged');
  });
});

test('the call site lands as relatedInformation, its byte span in UTF-16 units', async () => {
  await withHost(NO_CLI, async ({ calls, diagnostics }) => {
    diagnostics.register(makeContext());
    const document = makeDocument({ text: TEXT });
    await calls.events.open[0](document);
    const [list] = [...calls.collections[0].entries.values()];
    const [info] = list.find((d) => d.range.start.line === 1).relatedInformation;
    assert.equal(info.location.uri, document.uri);
    const { start, end } = info.location.range;
    assert.deepEqual([start.line, start.character, end.character], [4, 17, 21]);
    assert.equal(TEXT.split('\n')[4].slice(start.character, end.character), '@use');
    assert.equal(info.message, 'the @use stencil this was expanded from');
  });
});

test('an entry with no related span, or no document to place it in, gets none', async () => {
  await withHost(NO_CLI, async ({ diagnostics }) => {
    const plain = { line: 1, col: 1, len: 1, severity: 'error', message: 'm', code: 'E_X' };
    assert.equal(diagnostics.toDiagnostic(plain, ['x'], { fsPath: '/a' }).relatedInformation, undefined);
    assert.equal(diagnostics.toDiagnostic({ ...plain, related: CALL }).relatedInformation, undefined);
  });
});

test('a --script-check line borrows the span from the copies\' own diagnostic',
  { skip: process.platform === 'win32' ? 'POSIX stub script' : false }, async () => {
    const dir = mkdtempSync(join(tmpdir(), 'stencil-vsce-'));
    const script = join(dir, 'demo.stc');
    const cli = join(dir, 'stencil');
    writeFileSync(script, TEXT);
    const note = '@line needs at least two points (from the @use stencil at 5:19)';
    writeFileSync(cli, `#!/bin/sh\necho "$2:2:9: error: ${note} [E_LINE_NEEDS_POINTS]"\n`
      + `echo "$2:2:9: error: some other message [E_LINE_NEEDS_POINTS]"\nexit 1\n`, { mode: 0o755 });
    try {
      await withHost({ 'stencil.cliPath': cli }, async ({ diagnostics }) => {
        const document = makeDocument({ path: script, text: TEXT });
        const copies = await diagnostics.collect(makeDocument({ text: TEXT }), { saved: false });
        const [mine, other] = await diagnostics.collect(document, { saved: true });
        assert.equal(mine.message, copies.find((d) => d.line === 2).message, 'the stub says what core says');
        assert.deepEqual(mine.related, CALL);
        assert.equal(other.related, undefined, 'only the diagnostic that is the same one');
      });
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });
