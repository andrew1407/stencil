// Route A, the hand-off: the URL the user's own browser is opened on, what a .stc, a
// .stencil or a picture becomes inside its fragment, and every refusal that comes first.
import test from 'node:test';
import assert from 'node:assert/strict';

import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { APP, PNG, open, openedPayload, withHost } from './helpers/webHost.js';

// ── Route A ──────────────────────────────────────────────────────────────────
test('open-in-web hands the script to the configured instance, in the fragment', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    open(vscode, { text: '@filter sepia\n' });
    await web.openInWeb();
    assert.equal(calls.opened.length, 1);
    assert.ok(String(calls.opened[0]).startsWith(`${APP}#stencil=`), 'the instance, then the fragment');
    assert.deepEqual(openedPayload(calls), { script: '@filter sepia\n' });
    assert.deepEqual(calls.errors, []);
  });
});

test('a local @source is sent anyway, with a warning — the app reports it itself', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    open(vscode, { text: '@source ./cat.png:\n  @crop 10%\n' });
    await web.openInWeb();
    assert.equal(calls.opened.length, 1, 'the hand-off still happens');
    assert.match(calls.warnings.at(-1), /\.\/cat\.png/);
    assert.match(calls.warnings.at(-1), /http\(s\) URL/);
  });
});

test('a remote @source is the app\'s to fetch, and draws no warning', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    open(vscode, { text: '@source https://cdn.example/i.png:\n  @crop 10%\n' });
    await web.openInWeb();
    assert.deepEqual(calls.warnings, []);
    assert.equal(openedPayload(calls).script, '@source https://cdn.example/i.png:\n  @crop 10%\n');
  });
});

test('a .stencil project opens as its image and its layout', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    open(vscode, {
      languageId: 'stencil-project', path: '/tmp/roof.stencil',
      text: JSON.stringify({ name: 'roof', image: { dataUrl: 'data:image/png;base64,AAAA', ext: '.png' }, layout: { lines: [] } }),
    });
    await web.openInWeb();
    assert.deepEqual(openedPayload(calls), {
      dataUrl: 'data:image/png;base64,AAAA', name: 'roof.png', layout: { lines: [] },
    });
  });
});

test('a .stcjs is refused by the hand-off and pointed at the console', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    open(vscode, { languageId: 'stencil-js', path: '/tmp/a.stcjs', text: 'stencil.rotateRight()' });
    await web.openInWeb();
    assert.equal(calls.opened.length, 0, 'the fragment carries scripts and pictures, never code');
    assert.equal(calls.errors.at(-1), web.STCJS_IS_CONSOLE_ONLY);
  });
});

test('nothing open, an unreadable project, and a bad instance each refuse before opening', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    vscode.window.activeTextEditor = undefined;
    await web.openInWeb();
    assert.equal(calls.errors.at(-1), web.OPEN_A_FILE);

    open(vscode, { languageId: 'stencil-project', text: '{ not json' });
    await web.openInWeb();
    assert.equal(calls.errors.at(-1), web.OPEN_A_FILE);

    open(vscode, { languageId: 'markdown', path: '/tmp/a.md', text: '# hi' });
    await web.openInWeb();
    assert.equal(calls.errors.at(-1), web.OPEN_A_FILE);
    assert.equal(calls.opened.length, 0);
  });
});

test('a bad stencil.webUrl stops the command, with the setting named', async () => {
  await withHost({ settings: { 'stencil.webUrl': 'file:///tmp/app.html' } }, async ({ calls, vscode, web }) => {
    open(vscode, { text: '@crop 10%\n' });
    await web.openInWeb();
    assert.equal(calls.opened.length, 0);
    assert.match(calls.errors.at(-1), /stencil\.webUrl/);
  });
});

test('a script too long for a URL is refused rather than silently truncated', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    open(vscode, { text: `# ${'x'.repeat(2000000)}\n` });
    await web.openInWeb();
    assert.equal(calls.opened.length, 0);
    assert.equal(calls.errors.at(-1), web.TOO_BIG);
  });
});

// A script with no @source acts on whatever is open, and a fresh tab holds nothing — so the
// hand-off brings a picture, the way the CLI's run-on-image command does.
test('a source-less script brings a picture with it', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'stencil-open-'));
  const path = join(dir, 'cat.png');
  writeFileSync(path, PNG);
  try {
    await withHost({ openDialog: [{ fsPath: path }] }, async ({ calls, vscode, web }) => {
      open(vscode, { text: '@crop 10%\n@filter bw\n' });
      await web.openInWeb();
      const payload = openedPayload(calls);
      assert.equal(payload.script, '@crop 10%\n@filter bw\n');
      assert.equal(payload.name, 'cat.png');
      assert.ok(payload.dataUrl.startsWith('data:image/png;base64,'));
    });
    // Inlining off: the picture stays out of the URL, and the script still goes.
    await withHost({
      openDialog: [{ fsPath: path }], settings: { 'stencil.webInlineImages': false },
    }, async ({ calls, vscode, web }) => {
      open(vscode, { text: '@crop 10%\n' });
      await web.openInWeb();
      assert.deepEqual(openedPayload(calls), { script: '@crop 10%\n' });
    });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('a cancelled pick still hands the script over — the app reports what it cannot do', async () => {
  await withHost({ openDialog: [] }, async ({ calls, vscode, web }) => {
    open(vscode, { text: '@crop 10%\n' });
    await web.openInWeb();
    assert.deepEqual(openedPayload(calls), { script: '@crop 10%\n' });
  });
});

test('a script that names its own source is never asked for a picture', async () => {
  await withHost({ openDialog: [{ fsPath: '/tmp/never.png' }] }, async ({ calls, vscode, web }) => {
    open(vscode, { text: '@source https://cdn.example/i.png:\n  @crop 10%\n' });
    await web.openInWeb();
    assert.deepEqual(openedPayload(calls), { script: '@source https://cdn.example/i.png:\n  @crop 10%\n' });
  });
});
