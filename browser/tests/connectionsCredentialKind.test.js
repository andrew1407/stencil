// The remembered credential kind (js/net/connectionManager.js): a proven admin token mints
// straight away, a pasted one starts unknown, and a reconnecting server stays listed.
import { test } from 'node:test';
import assert from 'node:assert';
import { ConnectionManager } from '../js/net/connectionManager.js';
import { makeMockServer, StubWS, deadSessionServer } from './helpers/connectionsRig.js';

// A saved ADMIN token cannot list projects, so probing it as a session token is a guaranteed 401
// before the mint rescues it: remembering what the credential IS removes the request.
test('a proven admin credential mints straight away on the next connect', async () => {
  const server = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  // First connect with an admin token: probe (401) → mint → verify. Three calls, once.
  await mgr.connect({ url: 'http://a:1', token: 'admin-token' });
  assert.deepStrictEqual(server.state.calls, ['GET /projects', 'POST /auth/token', 'GET /projects']);
  assert.strictEqual(mgr.get('http://a:1').credentialKind, 'admin', 'the kind is learned');
  // …and it is what gets saved, beside the URL — no token semantics changed.
  assert.deepStrictEqual(mgr.snapshot(), [{ url: 'http://a:1', token: 'admin-token', kind: 'admin' }]);
  // The NEXT session restores that: mint first, no doomed probe, no 401 anywhere.
  server.state.calls.length = 0;
  const next = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  await next.connect({ url: 'http://a:1', token: 'admin-token', kind: 'admin' });
  assert.deepStrictEqual(server.state.calls, ['POST /auth/token', 'GET /projects'],
    'straight to minting — the refused probe is gone');
  assert.strictEqual(next.get('http://a:1').status, 'connected');
});

test('a plain session token still probes first, and an admin one that dies expires', async () => {
  const server = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  // A working session token is used as-is: one request, no mint.
  await mgr.connect({ url: 'http://a:1', token: 'issued-token' });
  assert.deepStrictEqual(server.state.calls, ['GET /projects']);
  assert.strictEqual(mgr.get('http://a:1').credentialKind, '', 'nothing to remember');
  assert.deepStrictEqual(mgr.snapshot(), [{ url: 'http://a:1', token: 'issued-token' }]);
  // An admin credential the server has STOPPED accepting lands in the expired state,
  // exactly like any other refusal — the shortcut never hides a dead credential.
  const dead = makeMockServer({ requireToken: 'issued-token', adminToken: 'other-admin' });
  const m2 = new ConnectionManager({ fetchImpl: dead.fetchImpl, WebSocketImpl: StubWS });
  const err = await m2.connect({ url: 'http://b:2', token: 'admin-token', kind: 'admin' })
    .then(() => null, (e) => e);
  assert.ok(err?.expired, 'a refused mint is still an expired session');
  assert.deepStrictEqual(dead.state.calls, ['POST /auth/token'], 'and it cost ONE request');
  assert.deepStrictEqual(m2.expiredUrls, ['http://b:2']);
  assert.strictEqual(m2.get('http://b:2').status, 'expired');
});

test('the kind survives reconnect-one and adoption, but a pasted token starts unknown', async () => {
  const server = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'admin-token' });
  server.state.calls.length = 0;
  // Reusing the stored credential keeps what we learned…
  await mgr.reconnectOne('http://a:1');
  assert.deepStrictEqual(server.state.calls, ['POST /auth/token', 'GET /projects']);
  // …while a token the user PASTES is of unknown kind and probes first.
  server.state.calls.length = 0;
  await mgr.reconnectOne('http://a:1', 'issued-token');
  assert.deepStrictEqual(server.state.calls, ['GET /projects']);
  assert.strictEqual(mgr.get('http://a:1').credentialKind, '');
  // Adoption carries it too, so a dead-but-known admin row keeps its shortcut.
  const m2 = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  m2.adoptExpired({ url: 'http://a:1', token: 'admin-token', kind: 'admin' });
  assert.strictEqual(m2.get('http://a:1').credentialKind, 'admin');
});

// reconnectOne drops the old connection and adds the new one only once the handshake lands, while
// every status event in between re-renders — so the server must stay known throughout (user report).
test('a server being reconnected stays in the list for the whole handshake', async () => {
  const { fetchImpl } = makeMockServer();
  let mgr;
  const seen = [];
  mgr = new ConnectionManager({
    // Sampled from INSIDE the handshake — the exact window the list used to render empty.
    fetchImpl: async (u, i) => { seen.push({ known: mgr.knownUrls.length, row: mgr.get('http://a:1') }); return fetchImpl(u, i); },
    WebSocketImpl: StubWS,
  });
  await mgr.connect('http://a:1');
  assert.equal(mgr.knownUrls.length, 1);

  seen.length = 0;
  await mgr.reconnectOne('http://a:1');
  assert.ok(seen.length > 0, 'the reconnect really did handshake');
  assert.ok(seen.every((s) => s.known === 1), 'the url is known at every point in between');
  assert.ok(seen.every((s) => s.row && s.row.status === 'connecting'),
    '…and the row has something to render: "connecting"');
  // …and once it lands the real connection is what get() answers with, exactly once.
  assert.equal(mgr.knownUrls.length, 1);
  assert.equal(mgr.get('http://a:1').status, 'connected');
});
