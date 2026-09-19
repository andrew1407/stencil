// The facade's connection surface (js/console/stencilApi.js): connect/disconnect chaining,
// the address flag threaded through the create path, and the persisted server set.
import { test } from 'node:test';
import assert from 'node:assert';
import { ConnectionManager } from '../js/net/connectionManager.js';
import { installMemoryStorage } from './helpers/memoryStorage.js';
import { makeMockServer, StubWS, facadeApp } from './helpers/connectionsRig.js';

// ── Facade integration: connect/disconnect/reconnect/connections ──
test('stencil facade exposes the connection surface and chains', async () => {
  const { createStencil } = await import('../js/console/stencilApi.js');
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
  const { createStencil } = await import('../js/console/stencilApi.js');
  const server = makeMockServer();
  const app = facadeApp(server, {
    createBlankImage(opts) { app._blankOpts = opts; return Promise.resolve({ width: 2, height: 2 }); },
  });
  const stencil = createStencil(app);
  await stencil.connect('http://srv:8090');

  // Unknown address rejects BEFORE any local work runs.
  await assert.rejects(() => stencil.blank('#fff', { address: 'http://nope:1' }), /Not connected/);
  assert.equal(app._blankOpts, undefined);

  // Known address threads through to the shared create path.
  await stencil.blank('#fff', { address: 'http://srv:8090' });
  assert.equal(app._blankOpts.address, 'http://srv:8090');

  // Local (no address) keeps today's behaviour: no address key passed.
  app._blankOpts = undefined;
  await stencil.blank('#fff');
  assert.equal('address' in app._blankOpts, false);
});

test('facade newEditor({ address }) arms the server as the create target for the next image', async () => {
  const { createStencil } = await import('../js/console/stencilApi.js');
  const server = makeMockServer();
  // The server forbids image-less projects, so newEditor({ address }) does NOT create one:
  // it arms the address; the next image load creates it with real bytes.
  const app = facadeApp(server, {
    newEditor() { app._newed = true; app.pendingRemoteAddress = null; },
    async createRemoteBlank(address) { app.pendingRemoteAddress = address; return { address }; },
  });
  const stencil = createStencil(app);
  await stencil.connect('http://srv:8090');

  // newEditor validates synchronously (it returns the facade, not a promise, locally).
  assert.throws(() => stencil.newEditor({ address: 'http://nope:1' }), /Not connected/);
  const ret = await stencil.newEditor({ address: 'http://srv:8090' });
  assert.equal(ret, stencil);
  assert.ok(app._newed);
  assert.equal(app.remoteLink, undefined, 'no project is created up front');
  assert.equal(app.pendingRemoteAddress, 'http://srv:8090');
});

test('facade save() writes back when the session is server-linked, else flushes locally', async () => {
  const { createStencil } = await import('../js/console/stencilApi.js');
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
  const { layout } = await import('../js/ui/layout.js');
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
    const store = await import('../js/net/connectionStore.js');
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
