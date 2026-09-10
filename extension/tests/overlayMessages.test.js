// lib/overlay.js drives the in-page modal off window "message" events. The modal is
// mounted into an ARBITRARY page, so the host can post whatever it likes at it: the
// envelope {source:'stencil-modal'} is public. Only the extension's own iframe may be
// obeyed — a forged 'ready' would cancel the tab fallback, a forged 'close' would kill
// a live editor session.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mountStencilModal } from '../src/lib/overlay.js';

const el = (tag) => {
  const node = {
    tagName: tag, children: [], style: { setProperty() {}, cssText: '' }, dataset: {},
    className: '', _html: '', textContent: '', src: '', onclick: null, contentWindow: { id: tag },
    classList: { add() {}, remove() {}, contains: () => false },
    setAttribute() {}, getAttribute: () => null, addEventListener() {}, removeEventListener() {},
    append: (...n) => node.children.push(...n), appendChild: (n) => node.children.push(n), remove() { node.removed = true; },
    attachShadow: () => node,
    set innerHTML(v) { node._html = v; },
    get innerHTML() { return node._html; },
    querySelector: (sel) => (node._q[sel] ||= el(sel)),
    _q: {},
  };
  return node;
};

const mount = () => {
  const listeners = {};
  const created = [];
  const prior = { doc: globalThis.document, win: globalThis.window, chrome: globalThis.chrome };
  globalThis.document = {
    createElement: (t) => { const n = el(t); created.push(n); return n; },
    getElementById: () => null,
    body: el('body'),
    documentElement: el('html'),
    addEventListener() {}, removeEventListener() {},
  };
  globalThis.window = {
    matchMedia: () => ({ matches: false }),
    addEventListener: (t, fn) => { (listeners[t] ||= []).push(fn); },
    removeEventListener: (t, fn) => { listeners[t] = (listeners[t] || []).filter((f) => f !== fn); },
    open() {},
  };
  globalThis.chrome = { storage: { onChanged: { addListener() {}, removeListener() {} } }, runtime: { sendMessage() {} } };
  mountStencilModal('https://editor.example/#stencil=x', 'Stencil', 3000, {});
  const host = created.find((n) => n.id === 'stencil-ext-modal');
  const wrap = created.find((n) => n._html && n._html.includes('<iframe'));
  const frame = wrap.querySelector('iframe');
  const post = (data, source) => { for (const fn of listeners.message || []) fn({ data, source }); };
  const restore = () => { globalThis.document = prior.doc; globalThis.window = prior.win; globalThis.chrome = prior.chrome; };
  return { host, wrap, frame, post, restore, loading: host.querySelector('.loading') };
};

// NEGATIVE: the host page forging the envelope changes nothing.
test('a message from anywhere but the modal iframe is ignored', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const m = mount();
  try {
    m.post({ source: 'stencil-modal', type: 'ready' }, globalThis.window);
    m.post({ source: 'stencil-modal', type: 'close' }, globalThis.window);
    m.post({ source: 'stencil-modal', type: 'close' }, { id: 'some-other-frame' });
    m.post({ source: 'stencil-modal', type: 'close' }, null);
    assert.equal(m.loading.removed, undefined, 'a forged ready cleared the loading state');
    assert.equal(m.host.removed, undefined, 'a forged close tore the modal down');
  } finally { m.restore(); }
});

test('the modal iframe itself is obeyed', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const m = mount();
  try {
    m.post({ source: 'stencil-modal', type: 'ready' }, m.frame.contentWindow);
    assert.equal(m.loading.removed, true, 'a real ready must clear the loading state');
    m.post({ source: 'stencil-modal', type: 'close' }, m.frame.contentWindow);
    t.mock.timers.tick(500);
    assert.equal(m.host.removed, true, 'a real close must tear the modal down');
  } finally { m.restore(); }
});
