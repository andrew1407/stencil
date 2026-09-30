// The script hand-offs besides a plain run: the web app's Script window (`scriptMode: 'open'`),
// and the desktop app over a `stencil://` link — open, run, run incognito — with the same
// refusals and the same warning for a local @source the web hand-off gives.
import test from 'node:test';
import assert from 'node:assert/strict';

import { open, openedPayload, withHost } from './helpers/webHost.js';

const linkParams = (calls) => new URL(String(calls.opened.at(-1))).searchParams;

test('open-script-in-web puts the script in the Script window instead of running it', async () => {
  await withHost({}, async ({ calls, vscode, web }) => {
    open(vscode, { text: '@filter sepia\n' });
    await web.openScriptInWeb();
    assert.deepEqual(openedPayload(calls), { script: '@filter sepia\n', scriptMode: 'open' });
    await web.openInWeb();
    assert.equal(openedPayload(calls).scriptMode, undefined, 'the plain hand-off still runs');
  });
});

test('the desktop hand-offs open a stencil:// link: open, run and run incognito', async () => {
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
