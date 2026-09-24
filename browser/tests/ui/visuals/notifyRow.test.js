// The Visuals dialog's notifications row (js/ui/visuals/notifyRow.js): a select whose 'system'
// pick asks the browser first — refused, it says so and stays on the toasts — and whose reset
// goes back to the toasts.
import test from 'node:test';
import assert from 'node:assert/strict';

import { installMemoryStorage } from '../../helpers/memoryStorage.js';
import { createStubElement, installDom } from '../../helpers/dom.js';

installMemoryStorage();
const select = createStubElement('select', {
  options: [{ value: 'toast', textContent: 'In the app' }, { value: 'system', textContent: 'Browser notifications' }],
  dataset: { csEnhanced: '1' },   // the themed dropdown is not under test here
});
const balloon = createStubElement('stencil-notifications');
const toasts = [];
balloon.notify = (msg, type) => toasts.push([msg, type]);
const doc = installDom({}, {
  window: { dispatchEvent: () => {}, addEventListener: () => {} },
  CustomEvent: class { constructor(type, init) { this.type = type; this.detail = init?.detail; } },
});
doc.register('vs-notify-channel', select);
doc.register('notify-balloon', balloon);

const { notifyChannel, setNotifyChannel } = await import('../../../js/core/settings/notifyChannel.js');
const { SettingsController } = await import('../../../js/core/settings/controller.js');
const { wireNotifyRow } = await import('../../../js/ui/visuals/notifyRow.js');

let answer = 'granted';
const app = { settings: null };
app.settings = new SettingsController(app);
const row = wireNotifyRow(app, async () => answer);
const pick = async (v) => { select.value = v; await select.dispatch('change'); await new Promise((r) => setImmediate(r)); };

test('populate shows what is stored', () => {
  setNotifyChannel('system');
  row.populate();
  assert.equal(select.value, 'system');
  setNotifyChannel('toast');
  row.populate();
  assert.equal(select.value, 'toast');
});

test('picking the browser asks once and keeps the pick when granted', async () => {
  await pick('system');
  assert.equal(notifyChannel(), 'system');
  assert.equal(select.value, 'system');
  assert.equal(toasts.length, 0);
});

test('a refused ask says so on a toast and the row springs back to the app', async () => {
  setNotifyChannel('toast');
  for (const [word, text] of [['denied', /blocked/], ['unsupported', /no notifications/], ['default', /blocked/]]) {
    answer = word;
    await pick('system');
    assert.equal(notifyChannel(), 'toast', `${word}: the store is untouched`);
    assert.equal(select.value, 'toast', `${word}: the select shows the truth`);
    assert.match(toasts.at(-1)[0], text);
    assert.equal(toasts.at(-1)[1], 'fail');
  }
});

test('going back to the app asks nothing, and reset lands on the app too', async () => {
  answer = 'granted';
  await pick('system');
  let asked = false;
  const quiet = wireNotifyRow(app, async () => { asked = true; return 'denied'; });
  await pick('toast');
  assert.equal(notifyChannel(), 'toast');
  assert.equal(asked, false);
  setNotifyChannel('system');
  quiet.reset();
  assert.equal(notifyChannel(), 'toast');
});
