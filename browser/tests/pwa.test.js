// js/pwa.js: a newer deploy waiting behind the one serving this page is offered in a toast, and taken
// only on its click — the waiting worker is told to skip waiting and the page reloads once it has, so
// no page runs modules of two deploys. A first install takes control without a reload.
import { test, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from './helpers/dom.js';

installDom();
const { registerServiceWorker } = await import('../js/pwa.js');

const listeners = (target) => {
  const map = {};
  target.addEventListener = (t, fn) => { (map[t] ||= []).push(fn); };
  return (t, ...a) => (map[t] || []).forEach((fn) => fn(...a));
};

const rig = ({ controller = {}, waiting = null } = {}) => {
  const toasts = [];
  globalThis.document.getElementById = (id) => (id === 'notify-balloon' ? { notify: (msg, type, opts) => toasts.push({ msg, opts }) } : null);
  const reloads = [];
  globalThis.location = { reload: () => reloads.push(1) };
  const posted = [];
  const reg = { waiting, installing: null };
  const fireReg = listeners(reg);
  const sw = { controller, register: async () => reg };
  const fireSw = listeners(sw);
  Object.defineProperty(globalThis.navigator, 'serviceWorker', { value: sw, configurable: true });
  const worker = (state) => {
    const w = { state, postMessage: (m) => posted.push(m) };
    return { w, fire: listeners(w) };
  };
  return { toasts, reloads, posted, reg, fireReg, fireSw, worker };
};
const settle = () => new Promise((r) => setImmediate(r));
afterEach(() => { delete globalThis.navigator.serviceWorker; });

test('a deploy already waiting is offered, and taken only on the click: skip waiting, then one reload', async () => {
  const r = rig();
  const { w } = r.worker('installed');
  r.reg.waiting = w;
  registerServiceWorker();
  await settle();
  assert.equal(r.toasts.length, 1);
  assert.match(r.toasts[0].msg, /new version/i);
  assert.deepEqual(r.posted, [], 'nothing until the user asks');
  r.toasts[0].opts.onClick();
  assert.deepEqual(r.posted, [{ type: 'SKIP_WAITING' }]);
  r.fireSw('controllerchange');
  r.fireSw('controllerchange');
  assert.equal(r.reloads.length, 1, 'the page reloads once, onto the new deploy');
});

test('a deploy found while the page runs is offered once it has installed', async () => {
  const r = rig();
  registerServiceWorker();
  await settle();
  const { w, fire } = r.worker('installing');
  r.reg.installing = w;
  r.fireReg('updatefound');
  assert.equal(r.toasts.length, 0);
  w.state = 'installed';
  fire('statechange');
  assert.equal(r.toasts.length, 1);
});

test('a first install takes control with no toast and no reload', async () => {
  const r = rig({ controller: null });
  registerServiceWorker();
  await settle();
  const { w, fire } = r.worker('installing');
  r.reg.installing = w;
  r.fireReg('updatefound');
  w.state = 'installed';
  fire('statechange');
  r.fireSw('controllerchange');
  assert.deepEqual([r.toasts.length, r.reloads.length], [0, 0]);
});
