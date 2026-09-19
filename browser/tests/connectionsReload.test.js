// Live co-edit (js/net/remoteSync.js shouldReloadFromEvent): which peer event earns a reload,
// the load that waits for the NEW image, and the credentials a reconnect replays.
import { test } from 'node:test';
import assert from 'node:assert';
import { ConnectionManager } from '../js/net/connectionManager.js';
import { shouldReloadFromEvent } from '../js/net/remoteSync.js';
import { installFetchStub } from './helpers/fetchStub.js';
import { makeMockServer, StubWS, facadeApp } from './helpers/connectionsRig.js';

// ── Live co-edit: shouldReloadFromEvent (the reload decision) ──
const LINK = { address: 'http://localhost:8090', remoteId: 'p1', version: 5 };
const evt = (over = {}) => ({ type: 'project-event', event: 'updated', project: { id: 'p1', version: 7 }, ...over });
const OPTS = { now: 100000, lastLocalSaveAt: 0, isDrawing: false, connUrl: 'http://localhost:8090' };

test('shouldReloadFromEvent: reloads on a peer update with a newer version', () => {
  assert.equal(shouldReloadFromEvent(evt(), LINK, OPTS), true);
});
test('shouldReloadFromEvent: ignores a version <= our own', () => {
  assert.equal(shouldReloadFromEvent(evt({ project: { id: 'p1', version: 5 } }), LINK, OPTS), false);
  assert.equal(shouldReloadFromEvent(evt({ project: { id: 'p1', version: 4 } }), LINK, OPTS), false);
});
test('shouldReloadFromEvent: ignores a different project', () => {
  assert.equal(shouldReloadFromEvent(evt({ project: { id: 'other', version: 7 } }), LINK, OPTS), false);
});
test('shouldReloadFromEvent: ignores non-updated and non-project events', () => {
  assert.equal(shouldReloadFromEvent(evt({ event: 'created' }), LINK, OPTS), false);
  assert.equal(shouldReloadFromEvent({ type: 'pong' }, LINK, OPTS), false);
  assert.equal(shouldReloadFromEvent(null, LINK, OPTS), false);
});
test('shouldReloadFromEvent: never reloads a local-only session', () => {
  assert.equal(shouldReloadFromEvent(evt(), null, OPTS), false);
});
test('shouldReloadFromEvent: holds off while drawing', () => {
  assert.equal(shouldReloadFromEvent(evt(), LINK, { ...OPTS, isDrawing: true }), false);
});
test('shouldReloadFromEvent: suppresses our own save echo, reloads after the short window', () => {
  // now=100000, echo window 150ms: 100ms ago → our echo → suppressed.
  assert.equal(shouldReloadFromEvent(evt(), LINK, { ...OPTS, lastLocalSaveAt: 99900 }), false);
  // 1000ms ago → a peer change made right after our save → reload (not dropped).
  assert.equal(shouldReloadFromEvent(evt(), LINK, { ...OPTS, lastLocalSaveAt: 99000 }), true);
});
test('shouldReloadFromEvent: ignores events from a different server', () => {
  assert.equal(shouldReloadFromEvent(evt(), LINK, { ...OPTS, connUrl: 'http://other:8090' }), false);
});

// Loading over an EXISTING image must wait for the swap: resolving on "some image exists" returns
// before the decode lands, so ops chained after it act on the OLD picture.
test('facade load waits for the NEW image, not merely for one to exist', async () => {
  const { createStencil } = await import('../js/console/stencilApi.js');
  const server = makeMockServer();
  const firstImage = { width: 2, height: 2 };
  const app = facadeApp(server, { image: firstImage });
  let applied = false;
  app.loadImageFromFile = () => {
    // decode lands LATER, exactly like the real loadImageFromFile (no promise returned)
    setTimeout(() => { app.image = { width: 4, height: 4 }; applied = true; }, 30);
  };
  const stencil = createStencil(app);
  const fetchStub = installFetchStub({ ok: true, blob: { type: 'image/png' } });
  try {
    await stencil.load('http://x/y.png');
    assert.ok(applied, 'load resolved before the new image was applied');
    assert.notStrictEqual(app.image, firstImage);
  } finally {
    fetchStub.restore();
  }
});

test('reconnect() replays CREDENTIALS — reconnect-all survives a server restart', async () => {
  // Pre-fix _lastSet held bare urls: reconnect-all handshook tokenless, 401d on
  // an admin-gated server, and dropped every connection.
  const { fetchImpl } = makeMockServer({ requireToken: 'issued-token', adminToken: 'adm' });
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'adm' });
  await mgr.reconnect();
  assert.deepEqual(mgr.urls, ['http://a:1']);
  assert.equal(mgr.get('http://a:1').token, 'issued-token');   // fresh session
  assert.equal(mgr.get('http://a:1').credential, 'adm');       // credential kept
});
