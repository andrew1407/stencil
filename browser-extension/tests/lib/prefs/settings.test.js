// The editor page API gate in getSettings(): off until the user saves an editor URL of their own,
// and an existing explicit choice (URL and toggle) is honoured as saved.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installChromeStub } from '../../helpers/chromeStub.js';
import { getSettings, DEFAULT_EDITOR_URL, isSharedOrigin } from '../../../src/lib/prefs/settings.js';

const settingsWith = async (sync) => {
  const stub = installChromeStub({ sync });
  try { return await getSettings(); } finally { stub.restore(); }
};

test('fresh install: the editor page API is off and the default URL is only a fallback', async () => {
  const s = await settingsWith({});
  assert.equal(s.editorPageApi, false);
  assert.equal(s.editorPageApiToggle, true);
  assert.equal(s.editorUrlSet, false);
  assert.equal(s.editorUrl, DEFAULT_EDITOR_URL);
});

test('a user-set editor URL turns it on; a blank one does not', async () => {
  const set = await settingsWith({ editorUrl: ' http://localhost:5173/ ' });
  assert.equal(set.editorPageApi, true);
  assert.equal(set.editorUrlSet, true);
  assert.equal(set.editorUrl, 'http://localhost:5173/');
  const blank = await settingsWith({ editorUrl: '   ' });
  assert.equal(blank.editorPageApi, false);
});

test('an existing explicit setting is honoured: saved URL + toggle on stays on, toggle off stays off', async () => {
  const mine = 'https://stencil.example/';
  assert.equal((await settingsWith({ editorUrl: mine, editorPageApi: true })).editorPageApi, true);
  assert.equal((await settingsWith({ editorUrl: mine, editorPageApi: false })).editorPageApi, false);
  assert.equal((await settingsWith({ editorPageApi: true })).editorPageApi, false, 'the toggle alone is not enough');
});

test('a stored default (an old Options page saved it on any Save) is no choice: the relay stays off', async () => {
  const s = await settingsWith({ editorUrl: ` ${DEFAULT_EDITOR_URL} `, editorPageApi: true });
  assert.equal(s.editorPageApi, false);
  assert.equal(s.editorUrlSet, false, 'the Options field shows it as unset');
  assert.equal(s.editorUrl, DEFAULT_EDITOR_URL, 'and the editor still opens at the default');
  assert.equal(s.editorPageApiToggle, true);
});

test('the content bridge mirrors the default it refuses as a choice', async () => {
  const { readFileSync } = await import('node:fs');
  const bridge = readFileSync(new URL('../../../src/content/editorBridge.js', import.meta.url), 'utf8');
  assert.equal(/const DEFAULT_EDITOR_URL = '([^']+)';/.exec(bridge)?.[1], DEFAULT_EDITOR_URL);
});

test('the options page saves the typed URL verbatim (blank stays blank) and says the API waits for it', async () => {
  const { readFileSync } = await import('node:fs');
  const src = (f) => readFileSync(new URL(`../../../src/options/${f}`, import.meta.url), 'utf8');
  assert.doesNotMatch(src('general.js'), /\|\|\s*DEFAULT_EDITOR_URL/);
  assert.match(src('general.js'), /checked = editorPageApiToggle/);
  assert.match(src('options.html'), /Off until you set the <strong>Editor URL<\/strong> above yourself/);
});

// Where a copied "Copy Stencil link" points: the published app unless the user typed an http(s) address.
test('the share link URL: the published app by default, the user\'s own http(s) address when set', async () => {
  const { DEFAULT_SHARE_URL } = await import('../../../src/lib/prefs/settings.js');
  assert.equal(DEFAULT_SHARE_URL, 'https://andrew1407.github.io/stencil/');
  const fresh = await settingsWith({});
  assert.deepEqual([fresh.shareUrl, fresh.shareUrlSet], [DEFAULT_SHARE_URL, false]);
  for (const typed of ['http://localhost:8080/', 'https://stencil.example.org/app/', 'http://192.168.1.20:3000']) {
    const s = await settingsWith({ shareUrl: ` ${typed} ` });
    assert.deepEqual([s.shareUrl, s.shareUrlSet], [typed, true], typed);
  }
  for (const bad of ['', 'ftp://x/', 'javascript:alert(1)', 'not a url']) {
    const s = await settingsWith({ shareUrl: bad });
    assert.deepEqual([s.shareUrl, s.shareUrlSet], [DEFAULT_SHARE_URL, false], bad);
  }
});

test('an editor under a path shares its origin; one at the root, or no URL, does not', () => {
  for (const url of ['https://andrew1407.github.io/stencil/', 'https://host.example/apps/stencil/index.html']) assert.ok(isSharedOrigin(url), url);
  for (const url of ['http://localhost:8080/', 'https://stencil.example', 'https://stencil.example/index.html', '', 'not a url', 'file:///x/']) {
    assert.ok(!isSharedOrigin(url), url);
  }
});
