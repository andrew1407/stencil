// Mid-session re-mint (js/net/connectionManager.js, extension parity with connections.js
// req()): one retry with the credential, the kind it records, and the remote listing tags.
import { test } from 'node:test';
import assert from 'node:assert';
import { ServerConnection, REMOTE_FLAG } from '../../js/net/connectionManager.js';
import { makeMockServer, StubWS } from '../helpers/connectionsRig.js';

// ── Mid-session re-mint (extension parity: connections.js req()) ────────────
test('a mid-session 401 re-mints with the credential and retries exactly once', async () => {
  const opts = { requireToken: 'issued-token', adminToken: 'adm' };
  const { state, fetchImpl } = makeMockServer(opts);
  const c = new ServerConnection('http://a:1', { token: 'adm', fetchImpl, WebSocketImpl: StubWS });
  await c.handshake();
  assert.equal(c.token, 'issued-token');

  // The server restarts: it forgets the session and mints a NEW token now.
  state.requireToken = 'issued-token-2';
  opts.mintToken = 'issued-token-2';
  state.calls.length = 0;
  const list = await c.listProjects(); // 401 → re-mint with 'adm' → retry, in place
  assert.deepEqual(state.calls, ['GET /projects', 'POST /auth/token', 'GET /projects']);
  assert.ok(Array.isArray(list));
  assert.equal(c.token, 'issued-token-2', 'the freshly minted session is adopted');
  assert.equal(c.credential, 'adm', 'the credential itself is never replaced');
  assert.equal(c.status, 'connected', 'a rescued session never surfaces as expired');
});

test('the mid-session re-mint happens once only: a dead credential lands expired', async () => {
  const opts = { requireToken: 'issued-token', adminToken: 'adm' };
  const { state, fetchImpl } = makeMockServer(opts);
  const c = new ServerConnection('http://a:1', { token: 'adm', fetchImpl, WebSocketImpl: StubWS });
  await c.handshake();

  // The server restarts AND rotates its admin token: nothing we hold works.
  state.requireToken = 'issued-token-2';
  opts.adminToken = 'other-admin';
  state.calls.length = 0;
  const err = await c.listProjects().then(() => null, (e) => e);
  assert.match(err.message, /bad admin token/);
  assert.equal(err.expired, true, 'flagged for the UI: only a new token helps');
  // Exactly one re-mint attempt, and /auth/token's own 401 never re-mints again.
  assert.deepEqual(state.calls, ['GET /projects', 'POST /auth/token']);
  assert.equal(c.status, 'expired');
});

test('a credential-less session does not re-mint mid-session (nothing to mint with)', async () => {
  const { state, fetchImpl } = makeMockServer();
  const c = new ServerConnection('http://a:1', { fetchImpl, WebSocketImpl: StubWS });
  await c.handshake(); // open server: mints anonymously, credential stays ''
  state.requireToken = 'someone-elses-token';
  state.calls.length = 0;
  await assert.rejects(() => c.listProjects(), /bad token/);
  assert.deepEqual(state.calls, ['GET /projects'], 'no mint attempt without a credential');
});

test('a successful mid-session rescue records the credential kind, like handshake()', async () => {
  // The pasted credential probed fine at connect (kind unknown); after it re-mints a
  // working session mid-flight, the kind is recorded so the next connect skips the probe.
  const opts = { requireToken: 'sess1' };
  const { state, fetchImpl } = makeMockServer(opts);
  const c = new ServerConnection('http://a:1', { token: 'sess1', fetchImpl, WebSocketImpl: StubWS });
  await c.handshake();
  assert.equal(c.credentialKind, '');
  state.requireToken = 'issued-token-2';
  opts.mintToken = 'issued-token-2';
  await c.listProjects();
  assert.equal(c.credentialKind, 'admin');
});

test('listProjects tags every record remote with its serverUrl', async () => {
  const { fetchImpl } = makeMockServer({ projects: [{ id: 'p_a_b', name: 'X', version: 1 }] });
  const c = new ServerConnection('http://srv:9', { token: 't', fetchImpl, WebSocketImpl: StubWS });
  await c.handshake();
  const list = await c.listProjects();
  assert.equal(list.length, 1);
  assert.equal(list[0][REMOTE_FLAG], true);
  assert.equal(list[0].serverUrl, 'http://srv:9');
});
