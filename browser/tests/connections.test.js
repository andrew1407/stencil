// The connection client (js/net/connectionManager.js): secure-by-default URLs, invite links
// carrying a token in the fragment, and the handshake that issues or validates one.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import {
  normalizeUrl, wsUrl, ServerConnection, ConnectionManager, isLoopbackHost, isInsecureRemote,
  parseInviteUrl, buildInviteUrl,
} from '../js/net/connectionManager.js';
import { makeMockServer, StubWS } from './helpers/connectionsRig.js';

test('normalizeUrl is secure by default: bare remote → https, loopback → http', () => {
  // Bare REMOTE host defaults to https (don't leak a token over cleartext).
  assert.equal(normalizeUrl('host:8090'), 'https://host:8090');
  // Loopback keeps plaintext http (dev servers; bytes never leave the machine).
  assert.equal(normalizeUrl('localhost:8090'), 'http://localhost:8090');
  assert.equal(normalizeUrl('127.0.0.1:8090'), 'http://127.0.0.1:8090');
  // An explicit scheme is preserved (deliberate opt-in), trailing slash stripped.
  assert.equal(normalizeUrl('http://host:8090/'), 'http://host:8090');
  assert.equal(normalizeUrl('  https://h:1/  '), 'https://h:1');
  assert.throws(() => normalizeUrl(''));
});

// ── Invite links: `<url>#token=<value>` ─────────────────────────────────────
test('parseInviteUrl splits the #token= fragment off the URL', () => {
  assert.deepEqual(parseInviteUrl('http://localhost:8090#token=abc123'),
    { url: 'http://localhost:8090', token: 'abc123' });
  // Encoded token values decode; a bare host works too (normalizeUrl runs after).
  assert.deepEqual(parseInviteUrl('srv:8090#token=a%2Bb'), { url: 'srv:8090', token: 'a+b' });
  // No fragment, a non-token fragment, or an empty token → pass through untouched.
  assert.deepEqual(parseInviteUrl('http://h:1'), { url: 'http://h:1', token: '' });
  assert.deepEqual(parseInviteUrl('http://h:1#other=x'), { url: 'http://h:1#other=x', token: '' });
  assert.deepEqual(parseInviteUrl('http://h:1#token='), { url: 'http://h:1#token=', token: '' });
});

test('buildInviteUrl builds <normalized-url>#token=<encoded token>', () => {
  assert.equal(buildInviteUrl('http://localhost:8090/', 'abc123'), 'http://localhost:8090#token=abc123');
  assert.equal(buildInviteUrl('localhost:8090', 'a+b'), 'http://localhost:8090#token=a%2Bb');
});

test('normalizeUrl keeps its behavior for fragment-carrying URLs (origin drops them)', () => {
  assert.equal(normalizeUrl('http://h:1#token=x'), 'http://h:1');
  assert.equal(normalizeUrl('localhost:8090#token=x'), 'http://localhost:8090');
});

test('connect() with an invite link strips the fragment and adopts the token as credential', async () => {
  const { fetchImpl } = makeMockServer({ requireToken: 'sess-tok' });
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect('http://a:1#token=sess-tok');
  assert.deepEqual(mgr.urls, ['http://a:1'], 'the fragment never reaches the url');
  assert.equal(mgr.get('http://a:1').token, 'sess-tok');
  assert.equal(mgr.get('http://a:1').credential, 'sess-tok', 'the fragment token feeds the credential flow');
  assert.deepEqual(mgr.snapshot(), [{ url: 'http://a:1', token: 'sess-tok' }]);
});

test('an explicitly supplied token wins over the invite fragment', async () => {
  // Only 'real-tok' is accepted; the fragment carries a bogus one.
  const { fetchImpl } = makeMockServer({ requireToken: 'real-tok', adminToken: 'nope' });
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1#token=bogus', token: 'real-tok' });
  assert.equal(mgr.get('http://a:1').credential, 'real-tok');
});

test('mintInvite mints a FRESH session with the credential and returns the link', async () => {
  const opts = { requireToken: 'issued-token', adminToken: 'adm' };
  const { state, fetchImpl } = makeMockServer(opts);
  const c = new ServerConnection('http://a:1', { token: 'adm', fetchImpl, WebSocketImpl: StubWS });
  await c.handshake();
  opts.mintToken = 'invite-tok';
  state.calls.length = 0;
  const link = await c.mintInvite();
  assert.equal(link, 'http://a:1#token=invite-tok');
  assert.deepEqual(state.calls, ['POST /auth/token'], 'one mint, nothing else');
  assert.equal(c.token, 'issued-token', 'the connection keeps its own session');
});

test('the invite round-trips: minting on one side, connecting with the link on the other', async () => {
  const opts = { requireToken: 'issued-token', adminToken: 'adm' };
  const { fetchImpl } = makeMockServer(opts);
  const c = new ServerConnection('http://a:1', { token: 'adm', fetchImpl, WebSocketImpl: StubWS });
  await c.handshake();
  const link = await c.mintInvite();  // mints 'issued-token' again
  const peer = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await peer.connect(link);
  assert.equal(peer.get('http://a:1').status, 'connected');
});

test('the connect modal offers Invite on credentialed connected rows', () => {
  const src = readFileSync(new URL('../js/ui/connect/connectModal.js', import.meta.url), 'utf8');
  assert.ok(src.includes('conn && conn.connected && conn.credential'), 'gated to credentialed live rows');
  assert.ok(src.includes('conn.mintInvite()'));
  assert.ok(src.includes('navigator.clipboard.writeText(link)'));
  assert.match(src, /Invite link copied/);
});

test('isLoopbackHost / isInsecureRemote classify the connection', () => {
  assert.equal(isLoopbackHost('localhost'), true);
  assert.equal(isLoopbackHost('127.0.0.1'), true);
  assert.equal(isLoopbackHost('::1'), true);
  assert.equal(isLoopbackHost('example.com'), false);
  // Only cleartext-to-a-remote-host is flagged insecure.
  assert.equal(isInsecureRemote('http://example.com:8090'), true);
  assert.equal(isInsecureRemote('http://localhost:8090'), false);
  assert.equal(isInsecureRemote('https://example.com:8090'), false);
});

test('wsUrl maps http(s) origin to ws(s)/ws', () => {
  assert.equal(wsUrl('http://h:8090'), 'ws://h:8090/ws');
  assert.equal(wsUrl('https://h:8090'), 'wss://h:8090/ws');
});

test('handshake issues a token when none is supplied', async () => {
  const { state, fetchImpl } = makeMockServer();
  const c = new ServerConnection('host:8090', { fetchImpl, WebSocketImpl: StubWS });
  await c.handshake();
  assert.equal(c.token, 'issued-token');
  assert.ok(c.connected);
  assert.ok(state.calls.includes('POST /auth/token'));
});

test('handshake validates a supplied token via GET /projects', async () => {
  const { state, fetchImpl } = makeMockServer({ requireToken: 'tkn' });
  const ok = new ServerConnection('host:8090', { token: 'tkn', fetchImpl, WebSocketImpl: StubWS });
  await ok.handshake();
  assert.ok(state.calls.includes('GET /projects'));

  const bad = new ServerConnection('host:8090', { token: 'wrong', fetchImpl, WebSocketImpl: StubWS });
  await assert.rejects(() => bad.handshake(), /HTTP 401|bad token/);
});

test('handshake mint-falls-back when the supplied token is the ADMIN token', async () => {
  // Desktop parity: pasting the server's admin token can't list projects, but it
  // CAN mint a session token — the handshake swaps to the minted one.
  const { state, fetchImpl } = makeMockServer({ requireToken: 'issued-token', adminToken: 'adm' });
  const c = new ServerConnection('host:8090', { token: 'adm', fetchImpl, WebSocketImpl: StubWS });
  await c.handshake();
  assert.equal(c.token, 'issued-token');
  assert.ok(c.connected);
  assert.ok(state.calls.includes('POST /auth/token'));
});
