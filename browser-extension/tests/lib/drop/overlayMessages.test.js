// lib/drop/overlay.js drives the in-page modal off window "message" events. The modal is
// mounted into an ARBITRARY page, so the host can post whatever it likes at it: the
// envelope {source:'stencil-modal'} is public. Only the extension's own iframe may be
// obeyed — a forged 'ready' would cancel the tab fallback, a forged 'close' would kill
// a live editor session.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mountStencilModal } from '../../../src/lib/drop/overlay.js';
import { installDom, stubDoc, stubEl, stubWin } from '../../helpers/domStub.js';

// The shell reaches for its own parts by selector, so each stub remembers what it made.
const el = (tag) => {
  const found = {};
  const posted = [];
  const node = stubEl(tag, {
    contentWindow: { id: tag, posted, postMessage: (data, origin) => posted.push({ data, origin }) },
    querySelector: (sel) => (found[sel] ||= el(sel)),
  });
  node.attachShadow = () => node;
  return node;
};

const mount = () => {
  const created = [];
  const sent = [];
  const win = stubWin();
  const restore = installDom({
    document: stubDoc({
      createElement: (t) => { const n = el(t); created.push(n); return n; },
      body: el('body'),
      documentElement: el('html'),
    }),
    window: win,
    chrome: { storage: { onChanged: { addListener() {}, removeListener() {} } }, runtime: { sendMessage: (m) => sent.push(m) } },
  });
  mountStencilModal('https://editor.example/#stencil=x', 'Stencil', 3000, {});
  const host = created.find((n) => n.id === 'stencil-ext-modal');
  const wrap = created.find((n) => n.innerHTML.includes('<iframe'));
  const frame = wrap.querySelector('iframe');
  const post = (data, source, origin = 'https://editor.example') => win.fire('message', { data, source, origin });
  return { host, wrap, frame, post, restore, sent, loading: host.querySelector('.loading') };
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
    assert.equal(m.loading.removed, false, 'a forged ready cleared the loading state');
    assert.equal(m.host.removed, false, 'a forged close tore the modal down');
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

// The host page reads a frame's src back through Resource Timing, so the payload never rides it.
test('the frame loads with no payload, which goes to the editor\'s origin once it is ready', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const m = mount();
  try {
    assert.equal(m.frame.src, 'https://editor.example/#stencil-await');
    m.post({ source: 'stencil-modal', type: 'ready' }, globalThis.window);
    assert.deepEqual(m.frame.contentWindow.posted, [], 'a forged ready is handed nothing');
    m.post({ source: 'stencil-modal', type: 'ready' }, m.frame.contentWindow);
    assert.deepEqual(m.frame.contentWindow.posted, [
      { data: { source: 'stencil-extension', type: 'handoff', hash: '#stencil=x' }, origin: 'https://editor.example' },
    ]);
  } finally { m.restore(); }
});

test('an editor that answers from another origin gets nothing: the payload opens in a full tab', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const m = mount();
  try {
    m.post({ source: 'stencil-modal', type: 'ready' }, m.frame.contentWindow, 'https://elsewhere.example');
    assert.deepEqual(m.frame.contentWindow.posted, []);
    assert.deepEqual(m.sent, [{ type: 'stencil-open-tab', url: 'https://editor.example/#stencil=x' }]);
    t.mock.timers.tick(500);
    assert.equal(m.host.removed, true);
  } finally { m.restore(); }
});
