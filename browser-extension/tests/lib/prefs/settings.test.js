// The editor page API gate in getSettings(): off until the user saves an editor URL of their own,
// and an existing explicit choice (URL and toggle) is honoured as saved.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installChromeStub } from '../../helpers/chromeStub.js';
import { getSettings, DEFAULT_EDITOR_URL } from '../../../src/lib/prefs/settings.js';

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
  assert.equal((await settingsWith({ editorUrl: DEFAULT_EDITOR_URL, editorPageApi: true })).editorPageApi, true);
  assert.equal((await settingsWith({ editorUrl: DEFAULT_EDITOR_URL, editorPageApi: false })).editorPageApi, false);
  assert.equal((await settingsWith({ editorPageApi: true })).editorPageApi, false, 'the toggle alone is not enough');
});

test('the options page saves the typed URL verbatim (blank stays blank) and says the API waits for it', async () => {
  const { readFileSync } = await import('node:fs');
  const src = (f) => readFileSync(new URL(`../../../src/options/${f}`, import.meta.url), 'utf8');
  assert.doesNotMatch(src('general.js'), /\|\|\s*DEFAULT_EDITOR_URL/);
  assert.match(src('general.js'), /checked = editorPageApiToggle/);
  assert.match(src('options.html'), /Off until you set the <strong>Editor URL<\/strong> above yourself/);
});
