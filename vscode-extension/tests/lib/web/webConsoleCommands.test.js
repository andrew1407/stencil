// Route B, the console: what each command evaluates in the page, what it says when the
// browser or the page will not answer, and where the result goes.
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { makeDocument, makeEditor, makeSelection } from '../../helpers/vscodeStub.js';
import { APP, PNG, open, sent, withHost } from '../../helpers/webHost.js';

// ── Route B ──────────────────────────────────────────────────────────────────
test('run-in-console sends a .stc as data to execScript, after waiting for the facade', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    open(vscode, { text: '@crop 10%\n' });
    await web.runInWebConsole();
    assert.deepEqual(calls.debugConfigs.map((d) => d.config.url), [APP]);
    assert.deepEqual(sent(calls), [
      'typeof window.stencil',
      'await window.stencil.execScript("@crop 10%\\n")',
    ]);
  });
});

test('a .stcjs runs verbatim — the only way it runs at all', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    open(vscode, { languageId: 'stencil-js', path: '/tmp/a.stcjs', text: 'stencil.rotateRight().apply({ filter: "sepia" })' });
    await web.runInWebConsole();
    assert.equal(sent(calls).at(-1), 'stencil.rotateRight().apply({ filter: "sepia" })');
  });
});

test('a plain .js joins in only once its first line says so', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    open(vscode, { languageId: 'javascript', path: '/tmp/a.js', text: 'stencil.undo()' });
    await web.runInWebConsole();
    assert.equal(calls.errors.at(-1), web.OPEN_A_FILE);

    open(vscode, { languageId: 'javascript', path: '/tmp/a.js', text: '// @use stencil\nstencil.undo()' });
    await web.runInWebConsole();
    assert.equal(sent(calls).at(-1), '// @use stencil\nstencil.undo()');
  });
});

test('the result and a page-side throw both land in the Stencil output channel', async () => {
  await withHost({ debugAnswers: [{ result: "'object'" }, { result: '400 x 300' }] },
    async ({ calls, vscode, web }) => {
      open(vscode, { languageId: 'stencil-js', path: '/tmp/a.stcjs', text: 'stencil.imageSize' });
      await web.runInWebConsole();
      const channel = calls.channels.at(-1);
      assert.equal(channel.name, web.OUTPUT_NAME);
      assert.deepEqual(channel.lines, ['> stencil.imageSize', '400 x 300']);
      assert.ok(channel.shown > 0, 'the answer is shown, not buried');
    });
  await withHost({ debugAnswers: [{ result: "'object'" }, new Error('No image loaded to crop')] },
    async ({ calls, vscode, web }) => {
      open(vscode, { text: '@crop 10%\n' });
      await web.runInWebConsole();
      assert.equal(calls.channels.at(-1).lines.at(-1), 'error: No image loaded to crop');
    });
});

test('a browser that will not start is reported, and nothing is evaluated', async () => {
  await withHost({ startDebugging: false }, async ({ calls, vscode, web }) => {
    open(vscode, { text: '@crop 10%\n' });
    await web.runInWebConsole();
    assert.equal(calls.sessions.length, 0);
    assert.match(calls.errors.at(-1), /Chrome or Edge/);
  });
});

test('a page that never boots is reported rather than run against', async () => {
  await withHost({ debugAnswers: [] }, async ({ calls, web }) => {
    await web.runExpression('stencil.undo()', { timeoutMs: 200 });
    assert.match(calls.errors.at(-1), /never finished booting/);
    assert.ok(!sent(calls).includes('stencil.undo()'));
  });
});

test('the selection wins over the file; with none, a typed expression stands in', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    const document = makeDocument({ languageId: 'stencil-js', path: '/tmp/a.stcjs', text: 'a\nb\nc' });
    vscode.window.activeTextEditor = makeEditor(document, makeSelection('stencil.zoomFit()'));
    await web.runSelectionInWebConsole();
    assert.equal(sent(calls).at(-1), 'stencil.zoomFit()');
  });
  await withHost({ inputBox: ['stencil.lines.length'] }, async ({ calls, vscode, web }) => {
    const document = makeDocument({ languageId: 'stencil-js', path: '/tmp/a.stcjs', text: 'x' });
    vscode.window.activeTextEditor = makeEditor(document, makeSelection(''));
    await web.runSelectionInWebConsole();
    assert.equal(sent(calls).at(-1), 'stencil.lines.length');
  });
});

test('a selection inside a .stc is still handed over as a script, not as code', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    const document = makeDocument({ text: '@crop 10%\n@filter bw\n' });
    vscode.window.activeTextEditor = makeEditor(document, makeSelection('@filter bw\n'));
    await web.runSelectionInWebConsole();
    assert.equal(sent(calls).at(-1), 'await window.stencil.execScript("@filter bw\\n")');
  });
});

test('a dismissed prompt evaluates nothing', async () => {
  await withHost({ inputBox: [undefined] }, async ({ calls, vscode, web }) => {
    const document = makeDocument({ languageId: 'stencil-js', path: '/tmp/a.stcjs', text: 'x' });
    vscode.window.activeTextEditor = makeEditor(document, makeSelection(''));
    await web.runSelectionInWebConsole();
    assert.equal(calls.sessions.length, 0);
  });
});

test('an image opens by URL, or by the bytes of a picked file', async () => {
  await withHost({ inputBox: ['https://cdn.example/i.png'] }, async ({ calls, web }) => {
    await web.openImageInWeb();
    assert.equal(sent(calls).at(-1), 'await window.stencil.load("https://cdn.example/i.png")');
  });
  const dir = mkdtempSync(join(tmpdir(), 'stencil-img-'));
  const path = join(dir, 'cat.png');
  writeFileSync(path, PNG);
  try {
    // An empty answer means "pick a file instead"; a dismissed one means "never mind".
    await withHost({ inputBox: [''], openDialog: [{ fsPath: path }] }, async ({ calls, web }) => {
      await web.openImageInWeb();
      assert.match(sent(calls).at(-1), /^await window\.stencil\.load\("data:image\/png;base64,/);
    });
    await withHost({ inputBox: [undefined], openDialog: [{ fsPath: path }] }, async ({ calls, web }) => {
      await web.openImageInWeb();
      assert.equal(calls.sessions.length, 0);
    });
    await withHost({
      inputBox: [''], openDialog: [{ fsPath: path }], settings: { 'stencil.webInlineImages': false },
    }, async ({ calls, web }) => {
      await web.openImageInWeb();
      assert.equal(calls.sessions.length, 0);
      assert.match(calls.errors.at(-1), /stencil\.webInlineImages/);
    });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('register contributes exactly the browser commands, and disposes its channel', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    const context = { subscriptions: [] };
    web.register(context);
    open(vscode, { languageId: 'stencil-js', path: '/tmp/a.stcjs', text: 'stencil.undo()' });
    await web.runInWebConsole();
    for (const disposable of context.subscriptions) disposable.dispose?.();
    assert.deepEqual([...calls.commands.keys()].filter((id) => id.includes('Web')).sort(),
      ['stencil.openImageInWeb', 'stencil.openInWeb', 'stencil.openInWebIncognito',
        'stencil.runInWebConsole', 'stencil.runSelectionInWebConsole'].sort());
    assert.equal(calls.channels.at(-1).disposed, true);
  });
});
