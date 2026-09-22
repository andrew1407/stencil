// ConnectionManager (js/net/connectionManager.js): several servers deduped, disconnect and
// reconnect, the persisted snapshot, reorder, aggregated projects and the events feed.
import { test } from 'node:test';
import assert from 'node:assert';
import { ServerConnection, ConnectionManager } from '../../js/net/connectionManager.js';
import { makeMockServer, StubWS } from '../helpers/connectionsRig.js';

test('ConnectionManager connects multiple servers and dedupes', async () => {
  const a = makeMockServer(), b = makeMockServer();
  // Route per-host by choosing fetch on url.
  const fetchImpl = (url, init) => (new URL(url).host === 'a:1' ? a.fetchImpl : b.fetchImpl)(url, init);
  let changes = 0;
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS, onChange: () => { changes++; } });

  await mgr.connect(['http://a:1', 'http://b:2']);
  assert.deepEqual(mgr.urls, ['http://a:1', 'http://b:2']);
  await mgr.connect('http://a:1'); // already connected → no duplicate
  assert.equal(mgr.urls.length, 2);
  assert.ok(changes >= 2);
});

test('disconnect with no arg drops the most recent connection', async () => {
  const { fetchImpl } = makeMockServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect(['http://a:1', 'http://b:2']);
  mgr.disconnect();
  assert.deepEqual(mgr.urls, ['http://a:1']);
  mgr.disconnect('http://a:1');
  assert.deepEqual(mgr.urls, []);
});

test('reconnect re-establishes the last set', async () => {
  const { fetchImpl } = makeMockServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect(['http://a:1', 'http://b:2']);
  await mgr.reconnect();
  assert.deepEqual(mgr.urls, ['http://a:1', 'http://b:2']);
});

test('snapshot exposes the live set as {url, token} for persistence', async () => {
  const { fetchImpl } = makeMockServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'tkn-a' });
  await mgr.connect('http://b:2'); // handshake mints a session token, but…
  // …the ORIGINAL credential persists, never the minted session token — a
  // session dies with the server; the credential re-mints on reconnect.
  assert.deepEqual(mgr.snapshot(), [
    { url: 'http://a:1', token: 'tkn-a' },
    { url: 'http://b:2', token: '' },
  ]);
});

test('reorder permutes the connection order, firing onChange, and persists via snapshot', async () => {
  const { fetchImpl } = makeMockServer();
  let changes = 0;
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS, onChange: () => { changes++; } });
  await mgr.connect(['http://a:1', 'http://b:2', 'http://c:3']);
  const before = changes;
  // Move the last (c) to the front.
  mgr.reorder(['http://c:3', 'http://a:1', 'http://b:2']);
  assert.deepEqual(mgr.urls, ['http://c:3', 'http://a:1', 'http://b:2']);
  assert.equal(changes, before + 1, 'reorder fires exactly one onChange (persist + broadcast)');
  // snapshot (what saveServers persists) reflects the new order.
  assert.deepEqual(mgr.snapshot().map((s) => s.url), ['http://c:3', 'http://a:1', 'http://b:2']);
});

test('reorder is defensive: unknown urls are skipped, omitted current urls kept at the end', async () => {
  const { fetchImpl } = makeMockServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect(['http://a:1', 'http://b:2', 'http://c:3']);
  // Name only b (+ a bogus url); a and c must survive, appended in their original order.
  mgr.reorder(['http://b:2', 'http://zzz:9']);
  assert.deepEqual(mgr.urls, ['http://b:2', 'http://a:1', 'http://c:3']);
});

test('reconnect preserves the reordered set (reorder updates _lastSet)', async () => {
  const { fetchImpl } = makeMockServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect(['http://a:1', 'http://b:2']);
  mgr.reorder(['http://b:2', 'http://a:1']);
  await mgr.reconnect();
  assert.deepEqual(mgr.urls, ['http://b:2', 'http://a:1']);
});

test('reconnectOne re-establishes a single connection, keeping the rest', async () => {
  const { fetchImpl } = makeMockServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect(['http://a:1', 'http://b:2']);
  await mgr.reconnectOne('http://a:1');
  assert.deepEqual(mgr.urls.sort(), ['http://a:1', 'http://b:2']);
  // unknown url just connects it fresh rather than throwing
  await mgr.reconnectOne('http://c:3');
  assert.ok(mgr.has('http://c:3'));
});

test('remoteProjects aggregates across connections and survives an unreachable one', async () => {
  const a = makeMockServer({ projects: [{ id: 'p_a_a', name: 'A', version: 0 }] });
  const b = makeMockServer({ projects: [{ id: 'p_b_b', name: 'B', version: 0 }] });
  let aDown = false;
  const fetchImpl = (url, init) => {
    const host = new URL(url).host;
    if (host === 'down:0' || (host === 'a:1' && aDown)) throw new Error('connection refused');
    return (host === 'a:1' ? a.fetchImpl : b.fetchImpl)(url, init);
  };
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect(['http://a:1', 'http://b:2']);
  aDown = true;   // force one connection to fail on list
  const list = await mgr.remoteProjects();
  assert.equal(list.length, 1);
  assert.equal(list[0].name, 'B');
});

test('events feed emits project-event messages to listeners', async () => {
  const { fetchImpl } = makeMockServer();
  const c = new ServerConnection('http://h:1', { token: 't', fetchImpl, WebSocketImpl: StubWS });
  await c.handshake();
  let got = null;
  c.onEvent((msg) => { got = msg; });
  StubWS.last.fire('open');
  StubWS.last.fire('message', { data: JSON.stringify({ type: 'project-event', event: 'created', project: { id: 'p_x_y' } }) });
  assert.equal(got.event, 'created');
  assert.equal(got.project.id, 'p_x_y');
});

// A batch is Promise.allSettled: one server down no longer costs the others their session.
const partialFetch = (down) => {
  const { fetchImpl } = makeMockServer();
  return (url, init) => {
    if (new URL(url).host === down) throw new Error('connection refused');
    return fetchImpl(url, init);
  };
};

test('connect keeps the servers that answered when one in the batch fails', async () => {
  const mgr = new ConnectionManager({ fetchImpl: partialFetch('bad:9'), WebSocketImpl: StubWS });
  await mgr.connect(['http://bad:9', 'http://a:1', 'http://b:2']);
  assert.deepEqual(mgr.urls, ['http://a:1', 'http://b:2'], 'the good ones connect, in the order asked for');
});

test('a partly-failed connect still records the set, so reconnect() has something to replay', async () => {
  const mgr = new ConnectionManager({ fetchImpl: partialFetch('bad:9'), WebSocketImpl: StubWS });
  await mgr.connect(['http://bad:9', 'http://a:1']);
  assert.equal(mgr.reconnectable, true, '_lastSet was written despite the failure');
  mgr.disconnectAll();
  await mgr.reconnect();
  assert.deepEqual(mgr.urls, ['http://a:1']);
});

test('an all-unreachable batch leaves the last known set standing for a later reconnect', async () => {
  let allDown = false;
  const { fetchImpl } = makeMockServer();
  const mgr = new ConnectionManager({
    fetchImpl: (url, init) => {
      if (allDown) throw new Error('connection refused');
      return fetchImpl(url, init);
    },
    WebSocketImpl: StubWS,
  });
  await mgr.connect(['http://a:1', 'http://b:2']);
  allDown = true;
  await assert.rejects(() => mgr.reconnect(), /connection refused/);
  allDown = false;
  await mgr.reconnect();
  assert.deepEqual(mgr.urls, ['http://a:1', 'http://b:2']);
});

test('only an all-failed batch throws, and one failure keeps its own error', async () => {
  const mgr = new ConnectionManager({ fetchImpl: partialFetch('bad:9'), WebSocketImpl: StubWS });
  await assert.rejects(() => mgr.connect('http://bad:9'), /connection refused/);
  await assert.rejects(() => mgr.connect(['http://bad:9', 'http://bad:9']), /connection refused/);
  assert.deepEqual(mgr.urls, []);
});
