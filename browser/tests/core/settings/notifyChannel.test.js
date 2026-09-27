// Where a notice shows (js/core/settings/notifyChannel.js): a two-way choice, toast or system,
// stored app-wide and announced on the bus; anything unrecognised reads as the toasts.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createStubElement, installDom } from '../../helpers/dom.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');

const store = new Map();
globalThis.localStorage = {
  getItem: (k) => (store.has(k) ? store.get(k) : null),
  setItem: (k, v) => store.set(k, String(v)),
  removeItem: (k) => store.delete(k),
};
const events = [];
// Every id resolves to a stub whose select face counts as built, so the Visuals modal wires whole.
const stub = (id) => createStubElement('div', { id, dataset: { csEnhanced: '1' }, querySelector: () => stub() });
const doc = installDom({}, {
  window: { dispatchEvent: (e) => events.push(e), addEventListener: () => {} },
  CustomEvent: class { constructor(type, init) { this.type = type; this.detail = init?.detail; } },
});
doc.getElementById = (id) => (doc.els.has(id) ? doc.els.get(id) : doc.els.set(id, stub(id)).get(id));

const {
  NOTIFY_CHANNELS, NOTIFY_STORAGE_KEY, NOTIFY_EVENT, DEFAULT_NOTIFY_CHANNEL, NOTIFY_CHANNEL_LABELS,
  notifyChannel, setNotifyChannel, reloadNotifyChannel, normalizeNotifyChannel,
} = await import('../../../js/core/settings/notifyChannel.js');
const { visualsModalInner } = await import('../../../js/ui/visuals/markup.js');
const { StencilVisualsModal } = await import('../../../js/ui/visuals/modal.js');
const { createSettingsFacade } = await import('../../../js/console/settingsFacade.js');
const { SettingsController } = await import('../../../js/core/settings/controller.js');

const recordingApp = (calls) => ({
  settings: { setNotifyChannel: (v) => calls.push(v), setMotion() {}, setVisualColor() {} },
  accents: { themeMode: 'system', setThemeMode() {}, setAccent() {} },
  renderer: { redraw() {} }, storage: { saveSoon() {} }, input: { setHoldDrawDelay() {} },
});

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

test('the row is a select in the Visuals modal, offering the two channels in order', () => {
  const html = visualsModalInner();
  const section = html.slice(html.indexOf('<div class="vs-section">Notifications</div>'));
  assert.ok(section.length < html.length, 'the Notifications section is rendered');
  const select = /<select id="vs-notify-channel">([\s\S]*?)<\/select>/.exec(section);
  assert.ok(select, 'its control is a <select>');
  assert.deepEqual([...select[1].matchAll(/<option value="([a-z]+)">([^<]+)<\/option>/g)].map((m) => [m[1], m[2]]),
    NOTIFY_CHANNEL_LABELS.map(([k, label]) => [k, label]));
  assert.doesNotMatch(html, /type="checkbox" id="vs-notify/, 'a select, never a checkbox');
});

test('the Visuals modal wires the row, and Reset All puts it back on the toasts', () => {
  const calls = [];
  StencilVisualsModal.prototype.wire.call({}, recordingApp(calls));
  const select = doc.getElementById('vs-notify-channel');
  select.value = 'toast';
  select.dispatch('change');
  assert.deepEqual(calls, ['toast'], 'a pick on the row reaches the settings funnel');
  calls.length = 0;
  doc.getElementById('vs-reset').dispatch('click');
  assert.deepEqual(calls, [DEFAULT_NOTIFY_CHANNEL], 'Reset All resets the channel too');
});

test('the console facade reads the channel and writes it through the controller', () => {
  const app = {};
  app.settings = new SettingsController(app);
  const facade = createSettingsFacade({ app, guard: (o) => o }).settings();
  setNotifyChannel('toast');
  facade.notifyChannel = 'System';
  assert.equal(notifyChannel(), 'system', 'the setter normalises and stores');
  assert.equal(facade.notifyChannel, 'system', 'the getter reads the store');
  assert.equal(doc.getElementById('vs-notify-channel').value, 'system', 'and the row is repainted');
  assert.throws(() => { facade.notifyChannel = 'email'; }, /^Error: Unknown notification channel: email \(use toast \| system\)$/);
  assert.equal(notifyChannel(), 'system', 'a refused value changes nothing');
  facade.notifyChannel = 'toast';
});

test('the desktop combo carries the same two keys, in the same order, with the same default', () => {
  const cpp = read('../../../../desktop/src/dialogs/settings/SettingsDialogMotionRows.cpp');
  assert.match(cpp, /addItem\(tr\("In the app"\), "toast"\);\s*\n\s*notifyChannel->addItem\(tr\("System notifications"\), "system"\)/);
  assert.match(read('../../../../desktop/src/io/fileStore.hpp'), /QString notifyChannel = "toast";/);
});
