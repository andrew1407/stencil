// The facade's connection surface (js/console/stencilApi.js): connect/disconnect chaining,
// the address flag threaded through the create path, and the persisted server set.
import { test } from 'node:test';
import assert from 'node:assert';
import { ConnectionManager } from '../../js/net/connectionManager.js';
import { installMemoryStorage } from '../helpers/memoryStorage.js';
import { installDom, createStubElement } from '../helpers/dom.js';
import { makeMockServer, StubWS, facadeApp, withSettle } from '../helpers/connectionsRig.js';
import { installImageDecode } from '../helpers/projectTransferRig.js';

// The blank flow paints its fill on a scratch canvas; every other element is inert.
const scratchCanvas = () => ({ width: 0, height: 0, getContext: () => ({ fillRect() {} }),
  toBlob: (cb, type) => cb(new Blob(['png'], { type })) });
const until = async (ok) => { for (let i = 0; i < 50 && !ok(); i++) await new Promise((r) => setImmediate(r)); };

// ── Facade integration: connect/disconnect/reconnect/connections ──
test('stencil facade exposes the connection surface and chains', async () => {
  const { createStencil } = await import('../../js/console/stencilApi.js');
  const { fetchImpl } = makeMockServer();
  // Minimal app stub: only what createStencil touches at construction + connect.
  const app = {
    connections: new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS }),
    lines: [], storage: { store: { list: () => [] }, incognito: false },
    tabs: { onPeers() {} },
    activeProjectId: null,
  };
  const stencil = createStencil(app);

  assert.deepEqual(stencil.connections, []);
  const ret = await stencil.connect('http://srv:8090');
  assert.equal(ret, stencil, 'connect resolves to the facade for chaining');
  assert.deepEqual(stencil.connections, ['http://srv:8090']);

  assert.equal(stencil.disconnect(), stencil, 'disconnect returns the facade');
  assert.deepEqual(stencil.connections, []);

  // connections is read-only (guarded).
  assert.throws(() => { stencil.connections = ['x']; }, /read-only/);
});

// ── Facade: address flag threads/validates through the same create path ──

test('facade blank({ address }) validates the target and threads it to createBlankImage', async () => {
  const { createStencil } = await import('../../js/console/stencilApi.js');
  const doc = installDom({ createElement: (tag) => (tag === 'canvas' ? scratchCanvas() : createStubElement(tag)) });
  const server = makeMockServer();
  const app = withSettle(facadeApp(server));
  // The real createBlankImage → loadImageFromFile → settle chain runs; the decode lands the blank's size.
  const decode = installImageDecode((file, img) => {
    const [, w, h] = /blank-(\d+)x(\d+)/.exec(file.name);
    img.width = app.canvas.width = Number(w);
    img.height = app.canvas.height = Number(h);
    img.onload();
  });
  const creates = () => server.state.calls.filter((c) => c === 'POST /projects').length;
  try {
    const stencil = createStencil(app);
    await stencil.connect('http://srv:8090');

    // Unknown address rejects BEFORE any local work runs.
    await assert.rejects(() => stencil.blank('#fff', { address: 'http://nope:1' }), /Not connected/);
    assert.deepEqual(decode.reads(), []);

    // Known address threads through the shared create path: the blank is created on that server.
    await stencil.blank('#fff', { address: 'http://srv:8090' });
    await until(() => app.remoteLink);
    assert.equal(creates(), 1);
    assert.equal(app.remoteLink.address, 'http://srv:8090');

    // Local (no address) keeps today's behaviour: nothing is created on a server.
    await stencil.blank('#fff');
    await until(() => false);
    assert.equal(decode.reads().length, 2);
    assert.equal(creates(), 1);
    assert.equal(app.remoteLink, null);
  } finally { decode.restore(); doc.restore(); }
});

test('facade newEditor({ address }) arms the server as the create target for the next image', async () => {
  const { createStencil } = await import('../../js/console/stencilApi.js');
  const server = makeMockServer();
  // The server forbids image-less projects, so newEditor({ address }) does NOT create one:
  // it arms the address; the next image load creates it with real bytes.
  const app = facadeApp(server);
  const stencil = createStencil(app);
  await stencil.connect('http://srv:8090');
  const resets = () => app.calls.filter(([n]) => n === 'newTemporary').length;

  // newEditor validates synchronously (it returns the facade, not a promise, locally).
  assert.throws(() => stencil.newEditor({ address: 'http://nope:1' }), /Not connected/);
  assert.equal(resets(), 0, 'validated before the editor reset');
  const ret = await stencil.newEditor({ address: 'http://srv:8090' });
  assert.equal(ret, stencil);
  assert.equal(resets(), 1, 'newEditor reset the editor');
  assert.equal(app.remoteLink, null, 'no project is created up front');
  assert.equal(server.state.calls.filter((c) => c === 'POST /projects').length, 0);
  assert.equal(app.pendingRemoteAddress, 'http://srv:8090');
});

test('facade save() writes back when the session is server-linked, else flushes locally', async () => {
  const { createStencil } = await import('../../js/console/stencilApi.js');
  const server = makeMockServer();
  let saved = 0;
  let pushed = 0;
  const app = facadeApp(server, {
    storage: { store: { list: () => [] }, incognito: false, save() { saved++; } },
    saveToServer() { pushed++; return Promise.resolve(); },
  });
  const stencil = createStencil(app);

  assert.equal(stencil.save(), stencil);   // unlinked → local flush
  assert.equal(saved, 1);
  assert.equal(pushed, 0);

  app.remoteLink = { address: 'http://srv:9', remoteId: 'p_x', version: 0 };
  const ret = await stencil.save();         // linked → server write-back
  assert.equal(ret, stencil);
  assert.equal(pushed, 1);
});

test('connect modal + toolbar button are composed into the layout exactly once', async () => {
  const { layout } = await import('../../js/ui/layout.js');
  const markup = layout();
  const once = (needle) => assert.equal(markup.split(needle).length - 1, 1, `${needle} should appear once`);
  for (const id of [
    'connect-btn', 'connect-modal-overlay', 'connect-close',
    'connect-url', 'connect-token', 'connect-add', 'connect-reconnect', 'connect-list',
  ]) {
    once(`id="${id}"`);
  }
  // The server icon glyph is registered and used by the toolbar button.
  assert.ok(markup.includes('ic-server'), 'server icon should be present');
});

test('connectionStore persists the server set and the auto-connect preference', async () => {
  // Provide a localStorage so the otherwise-inert store reads/writes. Scoped to this
  // test: the rest of the file asserts the store is inert without one.
  const storage = installMemoryStorage();
  try {
    const store = await import('../../js/net/connectionStore.js');
    // default: nothing saved, auto-connect on
    assert.deepEqual(store.loadSavedServers(), []);
    assert.equal(store.getAutoConnect(), true);
    // round-trips the slimmed {url, token} set
    store.saveServers([{ url: 'http://a:1', token: 't1', extra: 'dropped' }, { bad: true }]);
    assert.deepEqual(store.loadSavedServers(), [{ url: 'http://a:1', token: 't1' }]);
    // explicit opt-out persists and is honoured
    store.setAutoConnect(false);
    assert.equal(store.getAutoConnect(), false);
    store.setAutoConnect(true);
    assert.equal(store.getAutoConnect(), true);
  } finally {
    storage.restore();
  }
});
