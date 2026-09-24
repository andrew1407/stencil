// The notice sinks (js/ui/shell/notifySinks.js) and the routing in notifications.js: the stack's
// notify() asks the stored channel, hands the notice to the browser's Notification API when the
// user chose it and it is granted, and to its own toast() otherwise — so no notice is lost.
import test from 'node:test';
import assert from 'node:assert/strict';

const store = new Map();
globalThis.localStorage = {
  getItem: (k) => (store.has(k) ? store.get(k) : null),
  setItem: (k, v) => store.set(k, String(v)),
  removeItem: (k) => store.delete(k),
};
globalThis.window = { dispatchEvent: () => {}, addEventListener: () => {} };
globalThis.CustomEvent = class { constructor(type, init) { this.type = type; this.detail = init?.detail; } };

const { ToastSink, SystemSink, pickSink } = await import('../../../js/ui/shell/notifySinks.js');
const { setNotifyChannel } = await import('../../../js/core/settings/notifyChannel.js');
const { StencilNotifications } = await import('../../../js/ui/shell/notifications.js');

// A Notification constructor stand-in: records each instance, answers `permission`.
const fakeApi = (permission = 'granted') => {
  const made = [];
  class FakeNotification {
    static permission = permission;
    static async requestPermission() { FakeNotification.permission = 'granted'; return 'granted'; }
    constructor(title, opts) { this.title = title; this.opts = opts; this.closed = false; made.push(this); }
    close() { this.closed = true; }
  }
  return { N: FakeNotification, made };
};

test('the toast sink is the stack itself', () => {
  const calls = [];
  const sink = new ToastSink({ toast: (...a) => calls.push(a) });
  assert.equal(sink.show('Saved', 'ok', { key: 'k' }), true);
  assert.deepEqual(calls, [['Saved', 'ok', { key: 'k' }]]);
});

test('the system sink delivers only with granted permission, as a tagged notification', () => {
  const { N, made } = fakeApi('granted');
  let focused = 0;
  const sink = new SystemSink({ api: () => N, focus: () => focused++ });
  assert.equal(sink.isAvailable(), true);
  let clicked = 0;
  assert.equal(sink.show('Assistant finished', 'info', { onClick: () => clicked++, key: 'chat' }), true);
  assert.equal(made.length, 1);
  assert.equal(made[0].title, 'Stencil');
  assert.deepEqual(made[0].opts, { body: 'Assistant finished', tag: 'chat' });
  made[0].onclick();
  assert.equal(clicked, 1, 'a click runs the notice\'s own action');
  assert.equal(focused, 1, '…after bringing the page back');
  assert.equal(made[0].closed, true);

  for (const perm of ['default', 'denied']) {
    const other = new SystemSink({ api: () => fakeApi(perm).N });
    assert.equal(other.isAvailable(), false, `${perm} is not granted`);
    assert.equal(other.show('x'), false, `${perm}: hands the notice back`);
  }
  const none = new SystemSink({ api: () => undefined });
  assert.equal(none.isAvailable(), false, 'no API at all');
});

test('asking for permission answers unsupported / granted / the browser\'s word', async () => {
  assert.equal(await SystemSink.requestPermission(() => undefined), 'unsupported');
  assert.equal(await SystemSink.requestPermission(() => fakeApi('granted').N), 'granted');
  const { N } = fakeApi('default');
  assert.equal(await SystemSink.requestPermission(() => N), 'granted');
  N.requestPermission = async () => { throw new Error('gesture'); };
  N.permission = 'default';
  assert.equal(await SystemSink.requestPermission(() => N), 'denied');
});

test('pickSink: the system sink only while it is available, the toasts otherwise', () => {
  const toast = new ToastSink({ toast() {} });
  const granted = new SystemSink({ api: () => fakeApi('granted').N });
  const denied = new SystemSink({ api: () => fakeApi('denied').N });
  assert.equal(pickSink('system', { toast, system: granted }), granted);
  assert.equal(pickSink('system', { toast, system: denied }), toast);
  assert.equal(pickSink('toast', { toast, system: granted }), toast);
});

// The element's notify() is the seam every caller (utils.js notify) comes through.
test('the stack routes notify() by the stored channel and falls back to its own toasts', () => {
  const stack = new StencilNotifications();
  const toasts = [];
  stack.toast = (msg, type, opts) => toasts.push([msg, type, opts]);
  const { N, made } = fakeApi('granted');
  const prior = globalThis.Notification;
  globalThis.Notification = N;
  try {
    setNotifyChannel('toast');
    stack.notify('Saved', 'ok');
    assert.deepEqual(toasts, [['Saved', 'ok', {}]]);
    assert.equal(made.length, 0, 'the toast channel never touches the browser API');

    setNotifyChannel('system');
    stack.notify('Connected', 'info', { key: 'net' });
    assert.equal(made.length, 1, 'the system channel goes to the browser');
    assert.equal(made[0].opts.body, 'Connected');
    assert.equal(toasts.length, 1, '…and not to a toast');

    N.permission = 'denied';
    stack.notify('Lost', 'fail');
    assert.equal(made.length, 1);
    assert.deepEqual(toasts.at(-1), ['Lost', 'fail', {}], 'a revoked permission falls back to the toasts');
  } finally {
    globalThis.Notification = prior;
    setNotifyChannel('toast');
  }
});
