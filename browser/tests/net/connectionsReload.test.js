// Live co-edit (js/net/remoteSync.js shouldReloadFromEvent): which peer event earns a reload,
// the load that waits for the NEW image, the credentials a reconnect replays, and the events
// feed reopening itself after an unexpected drop.
import { test } from 'node:test';
import assert from 'node:assert';
import { ConnectionManager } from '../../js/net/connectionManager.js';
import { shouldReloadFromEvent } from '../../js/net/remoteSync.js';
import { installFetchStub } from '../helpers/fetchStub.js';
import { makeMockServer, StubWS, facadeApp } from '../helpers/connectionsRig.js';
import { installImageDecode } from '../helpers/projectTransferRig.js';

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
  const { createStencil } = await import('../../js/console/stencilApi.js');
  const server = makeMockServer();
  const firstImage = { width: 2, height: 2 };
  const app = facadeApp(server, { image: firstImage });
  let applied = false;
  // The real loadImageFromFile reads the file; its decode lands LATER (it returns no promise).
  const decode = installImageDecode(() => {
    setTimeout(() => { app.image = { width: 4, height: 4 }; applied = true; }, 30);
  });
  const stencil = createStencil(app);
  const fetchStub = installFetchStub({ ok: true, blob: new Blob(['x'], { type: 'image/png' }) });
  try {
    await stencil.load('http://x/y.png');
    assert.deepEqual(decode.reads(), ['y.png']);
    assert.ok(applied, 'load resolved before the new image was applied');
    assert.notStrictEqual(app.image, firstImage);
  } finally {
    fetchStub.restore();
    decode.restore();
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

// The feed's server, switchable down and back up; every socket it was handed is kept.
const flakyServer = () => {
  const srv = makeMockServer();
  const sockets = [];
  class WS extends StubWS { constructor(url) { super(url); sockets.push(this); } }
  const ctl = { down: false, sockets, WS };
  ctl.fetchImpl = async (url, init) => { if (ctl.down) throw new TypeError('Failed to fetch'); return srv.fetchImpl(url, init); };
  ctl.state = srv.state;
  return ctl;
};
const flush = async () => { for (let i = 0; i < 6; i++) await new Promise((r) => setImmediate(r)); };

test('an unexpected close reopens the feed on a capped backoff until the server answers', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout', 'Date'] });
  const srv = flakyServer();
  const mgr = new ConnectionManager({ fetchImpl: srv.fetchImpl, WebSocketImpl: srv.WS });
  await mgr.connect('http://a:1');
  const conn = mgr.get('http://a:1');
  srv.down = true;
  srv.sockets[0].fire('close');
  assert.equal(conn.status, 'error');
  t.mock.timers.tick(999);
  await flush();
  assert.equal(srv.sockets.length, 1, 'nothing before the first delay');
  t.mock.timers.tick(1);
  await flush();
  assert.equal(conn.status, 'error', 'still down: the handshake failed');
  t.mock.timers.tick(1999);
  await flush();
  assert.equal(srv.sockets.length, 1, 'the second delay doubles');
  srv.down = false;
  t.mock.timers.tick(1);
  await flush();
  assert.equal(conn.status, 'connected');
  assert.equal(conn.connected, true);
  assert.equal(srv.sockets.length, 2, 'one fresh events socket');
  const events = [];
  conn.onEvent((m) => events.push(m));
  srv.sockets[1].fire('open');
  assert.deepEqual(events.splice(0), [{ type: 'feed-resumed' }], 'the editor is told the feed missed a stretch');
  srv.sockets[1].fire('open');
  assert.equal(events.length, 0, 'once per redial');
  srv.sockets[1].fire('message', { data: JSON.stringify({ type: 'project-event', event: 'updated', project: { id: 'p' } }) });
  assert.equal(events.length, 1, 'peer edits flow again without a Reconnect');
  assert.deepEqual(mgr.urls, ['http://a:1']);
});

test('a refused credential stops the retries; a user disconnect never starts them', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout', 'Date'] });
  const srv = flakyServer();
  const mgr = new ConnectionManager({ fetchImpl: srv.fetchImpl, WebSocketImpl: srv.WS });
  await mgr.connect({ url: 'http://a:1', token: 'tkn' });
  const conn = mgr.get('http://a:1');
  srv.state.requireToken = 'another';
  srv.sockets[0].fire('close');
  t.mock.timers.tick(1000);
  await flush();
  assert.equal(conn.status, 'expired');
  const calls = srv.state.calls.length;
  t.mock.timers.tick(120_000);
  await flush();
  assert.equal(srv.state.calls.length, calls, 'no request after the refusal');

  const srv2 = flakyServer();
  const mgr2 = new ConnectionManager({ fetchImpl: srv2.fetchImpl, WebSocketImpl: srv2.WS });
  await mgr2.connect('http://b:2');
  mgr2.disconnect('http://b:2');
  t.mock.timers.tick(120_000);
  await flush();
  assert.equal(srv2.sockets.length, 1);
});
