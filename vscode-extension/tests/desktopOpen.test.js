// The script hand-offs besides the plain web one: the older Script-window id (`scriptMode:
// 'open'`) and the desktop app over a `stencil://` link — the modes both receivers parse and
// ignore, opening the Script window either way — with the web hand-off's refusals and warning.
import test from 'node:test';
import assert from 'node:assert/strict';

import { open, openedPayload, withHost } from './helpers/webHost.js';

const linkParams = (calls) => new URL(String(calls.opened.at(-1))).searchParams;

test('open-script-in-web marks its hand-off scriptMode open', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    open(vscode, { text: '@filter sepia\n' });
    await web.openScriptInWeb();
    assert.deepEqual(openedPayload(calls), { script: '@filter sepia\n', scriptMode: 'open' });
    await web.openInWeb();
    assert.equal(openedPayload(calls).scriptMode, undefined, 'the plain hand-off names no mode');
  });
});

test('the desktop hand-offs open a stencil:// link: open, run and incognito modes', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    open(vscode, { text: '@source https://cdn.example/i.png:\n  @crop 10%\n' });
    await web.openInDesktop();
    assert.ok(String(calls.opened.at(-1)).startsWith('stencil://open?'));
    assert.deepEqual([...linkParams(calls)], [
      ['script', '@source https://cdn.example/i.png:\n  @crop 10%\n'], ['scriptMode', 'open']]);
    await web.runInDesktop();
    assert.equal(linkParams(calls).get('scriptMode'), 'run');
    assert.equal(linkParams(calls).get('incognito'), null);
    await web.runInDesktopIncognito();
    assert.equal(linkParams(calls).get('incognito'), '1');
    assert.deepEqual(calls.errors, []);
  });
});

test('a desktop hand-off takes only a .stc, and warns about a local @source like the web one', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    open(vscode, { languageId: 'stencil-js', path: '/tmp/a.stcjs', text: 'stencil.undo()' });
    await web.runInDesktop();
    assert.equal(calls.opened.length, 0);
    assert.equal(calls.errors.at(-1), web.OPEN_A_SCRIPT);
    open(vscode, { text: '@source ./cat.png:\n  @crop 10%\n' });
    await web.runInDesktop();
    assert.equal(calls.opened.length, 1, 'sent anyway: the desktop reports the line itself');
    assert.match(calls.warnings.at(-1), /desktop cannot open \.\/cat\.png/);
  });
});
