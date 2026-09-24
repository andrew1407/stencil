// Where a notice shows (js/core/settings/notifyChannel.js): a two-way choice, toast or system,
// stored app-wide and announced on the bus; anything unrecognised reads as the toasts.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');

const store = new Map();
globalThis.localStorage = {
  getItem: (k) => (store.has(k) ? store.get(k) : null),
  setItem: (k, v) => store.set(k, String(v)),
  removeItem: (k) => store.delete(k),
};
const events = [];
globalThis.window = { dispatchEvent: (e) => events.push(e), addEventListener: () => {} };
globalThis.CustomEvent = class { constructor(type, init) { this.type = type; this.detail = init?.detail; } };

const {
  NOTIFY_CHANNELS, NOTIFY_STORAGE_KEY, NOTIFY_EVENT, DEFAULT_NOTIFY_CHANNEL, NOTIFY_CHANNEL_LABELS,
  notifyChannel, setNotifyChannel, reloadNotifyChannel, normalizeNotifyChannel,
} = await import('../../../js/core/settings/notifyChannel.js');

test('the default is the in-app toasts, and the select offers exactly the two channels', () => {
  assert.deepEqual(NOTIFY_CHANNELS, ['toast', 'system']);
  assert.equal(DEFAULT_NOTIFY_CHANNEL, 'toast');
  assert.equal(notifyChannel(), 'toast');
  assert.deepEqual(NOTIFY_CHANNEL_LABELS.map(([k]) => k), NOTIFY_CHANNELS);
});

test('a choice persists, announces, and survives a reload', () => {
  events.length = 0;
  assert.equal(setNotifyChannel('system'), 'system');
  assert.equal(notifyChannel(), 'system');
  assert.deepEqual(JSON.parse(store.get(NOTIFY_STORAGE_KEY)), { channel: 'system' });
  assert.equal(events.at(-1)?.type, NOTIFY_EVENT);
  assert.equal(events.at(-1)?.detail, 'system');
  assert.equal(reloadNotifyChannel(), 'system');
});

test('an unknown value, junk in the store, or a blocked store all read as the toasts', () => {
  assert.equal(normalizeNotifyChannel(' SYSTEM '), 'system');
  assert.equal(normalizeNotifyChannel('email'), 'toast');
  assert.equal(setNotifyChannel('email'), 'toast');
  store.set(NOTIFY_STORAGE_KEY, '{not json');
  assert.equal(reloadNotifyChannel(), 'toast');
  store.set(NOTIFY_STORAGE_KEY, JSON.stringify('system'));
  assert.equal(reloadNotifyChannel(), 'system', 'a bare string is read too');
  store.delete(NOTIFY_STORAGE_KEY);
  assert.equal(reloadNotifyChannel(), 'toast');
});

test('the row is a select in the Visuals modal, and the switch is on the console facade', () => {
  const markup = read('../../../js/ui/visuals/markup.js');
  assert.match(markup, /<div class="vs-section">Notifications<\/div>/);
  assert.match(markup, /<select id="vs-notify-channel">/);
  assert.doesNotMatch(markup, /type="checkbox" id="vs-notify/, 'a select, never a checkbox');
  const modal = read('../../../js/ui/visuals/modal.js');
  assert.match(modal, /const notifyRow = wireNotifyRow\(app\)/);
  assert.match(modal, /notifyRow\.reset\(\)/);
  const api = read('../../../js/console/settingsFacade.js');
  assert.match(api, /get notifyChannel\(\) \{ return notifyChannel\(\); \}/);
  assert.match(api, /set notifyChannel\(v\) \{ app\.settings\.setNotifyChannel\(v\); \}/);
  const controller = read('../../../js/core/settings/controller.js');
  assert.match(controller, /if \(!NOTIFY_CHANNELS\.includes\(c\)\)\s*\n?\s*throw new Error\(`Unknown notification channel/);
  assert.match(read('../../../js/ui/settings/settingMirrors.js'), /setVal\('vs-notify-channel', channel\)/);
  // The desktop's combo carries the same two keys, in the same order, with the same default.
  const cpp = read('../../../../desktop/src/dialogs/settings/SettingsDialogMotionRows.cpp');
  assert.match(cpp, /addItem\(tr\("In the app"\), "toast"\);\s*\n\s*notifyChannel->addItem\(tr\("System notifications"\), "system"\)/);
  assert.match(read('../../../../desktop/src/io/fileStore.hpp'), /QString notifyChannel = "toast";/);
});
