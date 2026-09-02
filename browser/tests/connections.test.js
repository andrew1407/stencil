import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import {
  normalizeUrl, wsUrl, ServerConnection, ConnectionManager, REMOTE_FLAG,
  isLoopbackHost, isInsecureRemote, parseInviteUrl, buildInviteUrl,
} from '../js/net/connectionManager.js';
import {
  requireConnection, createRemoteProject, saveRemoteProject, shouldReloadFromEvent, CONFLICT_MESSAGE,
} from '../js/net/remoteSync.js';
import { installMemoryStorage } from './helpers/memoryStorage.js';
import { installFetchStub } from './helpers/fetchStub.js';

// ── A mock fetch backed by an in-memory server model ──
// Routes the subset of the REST surface the client uses.
const makeMockServer = (opts = {}) => {
  const state = {
    projects: new Map(opts.projects ? opts.projects.map((p) => [p.id, p]) : []),
    seq: 0,
    calls: [],
    requireToken: opts.requireToken || null,
  };
  const json = (status, body) => ({
    ok: status >= 200 && status < 300,
    status,
    json: async () => body,
  });
  const fetchImpl = async (url, init = {}) => {
    const u = new URL(url);
    const method = init.method || 'GET';
    state.calls.push(`${method} ${u.pathname}`);
    const auth = (init.headers && init.headers.Authorization) || '';
    const token = auth.replace(/^Bearer\s+/, '');

    if (u.pathname === '/auth/token' && method === 'POST') {
      // opts.adminToken simulates a server whose mint endpoint is admin-gated.
      if (opts.adminToken && token !== opts.adminToken) return json(401, { code: 'unauthorized', message: 'bad admin token' });
      // opts.mintToken is read per call, so a test can rotate it (server restart).
      return json(200, { token: opts.mintToken || 'issued-token', expiresAt: 0 });
    }
    // Everything else needs a token.
    if (state.requireToken && token !== state.requireToken) return json(401, { code: 'unauthorized', message: 'bad token' });

    if (u.pathname === '/projects' && method === 'GET') {
      return json(200, { projects: Array.from(state.projects.values()) });
    }
    if (u.pathname === '/projects' && method === 'POST') {
      const body = JSON.parse(init.body);
      const id = 'p_srv' + (++state.seq) + '_a';
      const rec = {
        id,
        name: body.name || 'Untitled',
        source: body.source || '',
        resource: body.resource || '',
        hasImage: !!body.hasImage,
        version: 0,
        updatedAt: state.seq,
      };
      state.projects.set(id, rec);
      return json(201, rec);
    }
    // File upload (raw bytes): bump version, record dims for an original. The wire
    // FileWriteResponse carries no version, so the client re-reads it via GET.
    const fm = u.pathname.match(/^\/projects\/([^/]+)\/files\/([^/]+)$/);
    if (fm && method === 'POST') {
      const rec = state.projects.get(decodeURIComponent(fm[1]));
      if (!rec) return json(404, { code: 'notFound', message: 'gone' });
      const kind = fm[2];
      const w = parseInt(u.searchParams.get('w') || '0', 10);
      const h = parseInt(u.searchParams.get('h') || '0', 10);
      rec.version += 1;
      if (kind === 'original') { rec.hasImage = true; rec.imageW = w; rec.imageH = h; }
      state.bodies = state.bodies || [];
      state.bodies.push({ kind, bytes: init.body });
      return json(201, { path: `${rec.id}/${kind}.png`, w, h });
    }
    const m = u.pathname.match(/^\/projects\/([^/]+)$/);
    if (m && method === 'GET') {
      const rec = state.projects.get(decodeURIComponent(m[1]));
      return rec ? json(200, { project: rec, layout: rec.layout || {} }) : json(404, { code: 'notFound', message: 'gone' });
    }
    if (m && method === 'PUT') {
      const rec = state.projects.get(decodeURIComponent(m[1]));
      if (!rec) return json(404, { code: 'notFound', message: 'gone' });
      const body = JSON.parse(init.body);
      if (body.version !== rec.version) {
        return json(409, { code: 'conflict', message: 'stale version; reload and retry' });
      }
      if (body.name != null) rec.name = body.name;
      if (body.layout != null) rec.layout = body.layout;
      rec.version += 1;
      return json(200, rec);
    }
    if (m && method === 'DELETE') {
      state.projects.delete(decodeURIComponent(m[1]));
      return json(204, null);
    }
    return json(404, { code: 'notFound', message: 'no route ' + u.pathname });
  };
  return { state, fetchImpl };
};

// A no-op WebSocket so events-feed setup doesn't touch the network.
class StubWS {
  constructor(url) { this.url = url; this._l = {}; StubWS.last = this; }
  addEventListener(t, cb) { (this._l[t] ||= []).push(cb); }
  send() {}
  close() { (this._l.close || []).forEach((cb) => cb()); }
  fire(t, data) { (this._l[t] || []).forEach((cb) => cb(data)); }
}

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
  const src = readFileSync(new URL('../js/ui/connectModal.js', import.meta.url), 'utf8');
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
  const fetchImpl = (url, init) => {
    const host = new URL(url).host;
    if (host === 'down:0') throw new Error('connection refused');
    return (host === 'a:1' ? a.fetchImpl : b.fetchImpl)(url, init);
  };
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect(['http://a:1', 'http://b:2']);
  // Force one connection to fail on list by swapping its fetch.
  mgr.get('http://a:1')._fetch = () => { throw new Error('boom'); };
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

// ── remoteSync: create-on-server + save-back helpers ──
const connectOne = async (server, url = 'http://srv:9') => {
  const c = new ServerConnection(url, { token: 't', fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  await c.handshake();
  return c;
};

test('requireConnection validates a live connection or throws clearly', async () => {
  const server = makeMockServer();
  const mgr = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect('http://srv:8090');
  assert.equal(requireConnection(mgr, 'http://srv:8090').url, 'http://srv:8090');
  assert.throws(() => requireConnection(mgr, 'http://nope:1'), /Not connected/);
  assert.throws(() => requireConnection(null, 'x'), /No server connections/);
});

test('createRemoteProject routes to createProject + putFile(original) and tracks version', async () => {
  const server = makeMockServer();
  const conn = await connectOne(server);
  const link = await createRemoteProject(conn, {
    name: 'Shot', source: 'http://x/a.png', bytes: new Uint8Array([1, 2, 3]), ext: 'png', w: 4, h: 3,
  });
  assert.equal(link.address, 'http://srv:9');
  assert.match(link.remoteId, /^p_srv/);
  assert.equal(link.version, 1, 'create (v0) then original upload (→v1)');
  assert.ok(server.state.calls.includes('POST /projects'));
  assert.ok(server.state.calls.some((c) => /^POST \/projects\/.+\/files\/original$/.test(c)));
  assert.equal(server.state.bodies[0].kind, 'original');
  const rec = server.state.projects.get(link.remoteId);
  assert.equal(rec.hasImage, true);
  assert.equal(rec.imageW, 4);
});

test('createRemoteProject with no bytes creates a blank project (no upload)', async () => {
  const server = makeMockServer();
  const conn = await connectOne(server);
  const link = await createRemoteProject(conn, { name: 'Empty' });
  assert.equal(link.version, 0);
  assert.ok(!server.state.calls.some((c) => c.includes('/files/')));
  assert.equal(server.state.projects.get(link.remoteId).hasImage, false);
});

test('saveRemoteProject routes to updateProject + putFile(result) and tracks version', async () => {
  const server = makeMockServer();
  const conn = await connectOne(server);
  const link = await createRemoteProject(conn, { name: 'P', bytes: new Uint8Array([9]), w: 2, h: 2 });
  const next = await saveRemoteProject(conn, link, {
    name: 'P2', layout: { lines: [] }, bytes: new Uint8Array([7, 7]), w: 2, h: 2,
  });
  assert.equal(next.version, 3, 'update (→v2) then result upload (→v3)');
  assert.ok(server.state.calls.some((c) => /^PUT \/projects\//.test(c)));
  assert.ok(server.state.calls.some((c) => /\/files\/result$/.test(c)));
  const rec = server.state.projects.get(link.remoteId);
  assert.equal(rec.name, 'P2');
  assert.deepEqual(rec.layout, { lines: [] });
});

test('saveRemoteProject surfaces a 409 as a flagged conflict error', async () => {
  const server = makeMockServer();
  const conn = await connectOne(server);
  const link = await createRemoteProject(conn, { name: 'P', bytes: new Uint8Array([1]) });
  const stale = { ...link, version: 0 };   // server is at v1; v0 guard is stale
  await assert.rejects(
    () => saveRemoteProject(conn, stale, { name: 'x', layout: {} }),
    (err) => { assert.equal(err.conflict, true); assert.equal(err.message, CONFLICT_MESSAGE); return true; },
  );
});

test('move-to-server contract: adopt saveRemoteProject\'s refreshed version so a later field push does not 409', async () => {
  // Reproduces the #createServerFromLocal sequence: create (+ original upload) THEN save
  // the annotated layout. The layout save advances the server version again, so the link
  // to persist on the editor is the one saveRemoteProject returns — not the create link.
  // Discarding it left remoteLink.version stale, 409-ing the next colour/rename/expiry push.
  const server = makeMockServer();
  const conn = await connectOne(server);
  const createLink = await createRemoteProject(conn, { name: 'P', bytes: new Uint8Array([1, 2]), w: 2, h: 2 });
  const savedLink = await saveRemoteProject(conn, createLink, { name: 'P', layout: { lines: [] } });
  assert.ok(savedLink.version > createLink.version, 'the layout save advances the server version past create time');

  // A field push with the REFRESHED version succeeds…
  const ok = await conn.updateProject(createLink.remoteId, { name: 'renamed', version: savedLink.version });
  assert.equal(ok.version, savedLink.version + 1);
  // …while the STALE create-time version 409s (the exact failure this guards).
  await assert.rejects(
    () => conn.updateProject(createLink.remoteId, { name: 'again', version: createLink.version }),
    (err) => err.status === 409,
  );
});

test('deleteProject removes a project on the server', async () => {
  const server = makeMockServer({ projects: [{ id: 'p_a_b', name: 'X', version: 0 }] });
  const conn = await connectOne(server);
  await conn.deleteProject('p_a_b');
  assert.ok(!server.state.projects.has('p_a_b'));
  assert.ok(server.state.calls.includes('DELETE /projects/p_a_b'));
});

// ── Facade: address flag threads/validates through the same create path ──
const facadeApp = (server, extra = {}) => {
  const app = {
    connections: new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS }),
    lines: [],
    storage: { store: { list: () => [] }, incognito: false, save() {} },
    tabs: { onPeers() {} },
    activeProjectId: null,
    remoteLink: null,
    image: { width: 2, height: 2 },
    ...extra,
  };
  // The facade writes back via app.remoteSync.saveToServer(); mirror the real DrawingApp by
  // delegating the remote-sync namespace to the flat (per-test overridden) methods.
  app.remoteSync = {
    saveToServer: (...a) => app.saveToServer?.(...a),
    scheduleRemoteSync: (...a) => app.scheduleRemoteSync?.(...a),
    reloadRemoteActive: (...a) => app.reloadRemoteActive?.(...a),
    onServerProjectEvent: (...a) => app.onServerProjectEvent?.(...a),
  };
  return app;
};

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

// Regression: loading over an EXISTING image must wait for the swap. waitForImage used
// to resolve on "some image exists", so on a non-empty editor stencil.load() returned
// before the decode landed — ops chained after it (a §2.1 `image` op's filter/layout)
// hit the OLD picture and were then wiped when the new one arrived.
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

// ── A saved session the server no longer knows ──────────────────────────────
// Reported: opening the app with a stale saved token produced one red 401 in the
// console and NOTHING else — no row, no message, no way to sign in again.
// requireToken is what the mock MINTS, so an admin-token round genuinely revives it.
const deadSessionServer = () => makeMockServer({ requireToken: 'issued-token', adminToken: 'admin-token' });

test('a refused token lands in the expired state, not the unreachable one', async () => {
  const { fetchImpl } = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  const err = await mgr.connect({ url: 'http://a:1', token: 'stale' }).then(() => null, (e) => e);
  assert.ok(err, 'the connect still rejects — the caller decides what to say');
  assert.strictEqual(err.expired, true, 'and it is tagged as a dead SESSION');
  // Not live…
  assert.deepStrictEqual(mgr.urls, [], 'nothing may use a refused token');
  assert.strictEqual(mgr.has('http://a:1'), false);
  // …but remembered, with its URL, so the UI has a row to show and act on.
  assert.deepStrictEqual(mgr.expiredUrls, ['http://a:1']);
  assert.deepStrictEqual(mgr.knownUrls, ['http://a:1']);
  assert.strictEqual(mgr.isExpired('http://a:1'), true);
  assert.strictEqual(mgr.get('http://a:1').status, 'expired', 'its own dot state');
  assert.deepStrictEqual(mgr.snapshot(), [{ url: 'http://a:1', token: 'stale', expired: true }],
    'the saved URL survives — re-authenticating must not mean retyping the address');
});

test('an expired session is never retried on its own, and a new token revives it', async () => {
  const server = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'stale' }).catch(() => {});
  const after = server.state.calls.length;
  // Nothing in the manager touches it again by itself.
  assert.deepStrictEqual(await mgr.remoteProjects(), []);
  assert.strictEqual(server.state.calls.length, after, 'not one extra request');
  // The row's own Reconnect: a fresh mint first (all an open server needs)…
  await assert.rejects(() => mgr.reconnectOne('http://a:1', ''), /bad admin token/,
    'this server mints only for an admin token');
  assert.strictEqual(mgr.isExpired('http://a:1'), true, 'still expired, still shown');
  // …then the token the user pastes — the admin one mints a session, desktop parity.
  await mgr.reconnectOne('http://a:1', 'admin-token');
  assert.deepStrictEqual(mgr.urls, ['http://a:1']);
  assert.deepStrictEqual(mgr.expiredUrls, [], 'the expired row is gone once it is live');
  assert.strictEqual(mgr.get('http://a:1').status, 'connected');
  assert.strictEqual(mgr.get('http://a:1').token, 'issued-token');
});

test('a valid saved token still connects normally, and an UNREACHABLE server is not "expired"', async () => {
  const { fetchImpl } = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'issued-token' });
  assert.deepStrictEqual(mgr.urls, ['http://a:1']);
  assert.deepStrictEqual(mgr.expiredUrls, []);
  assert.strictEqual(mgr.get('http://a:1').status, 'connected');
  // A server that is simply down keeps the old behaviour — it may come back.
  const down = new ConnectionManager({
    fetchImpl: async () => { throw new TypeError('Failed to fetch'); }, WebSocketImpl: StubWS,
  });
  const err = await down.connect({ url: 'http://b:2', token: 't' }).then(() => null, (e) => e);
  assert.ok(err && !err.expired, 'not a credential problem');
  assert.deepStrictEqual(down.expiredUrls, [], 'and no expired row for it');
});

test('removing an expired row forgets it, exactly like disconnecting a live one', async () => {
  const { fetchImpl } = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'stale' }).catch(() => {});
  assert.strictEqual(mgr.reconnectable, true, 'reconnect-all can act on it');
  mgr.disconnect('http://a:1');
  assert.deepStrictEqual(mgr.knownUrls, [], 'the row is gone from the modal…');
  assert.deepStrictEqual(mgr.snapshot(), [], '…and from what gets saved');
  // disconnectAll clears them too.
  await mgr.connect({ url: 'http://a:1', token: 'stale' }).catch(() => {});
  mgr.disconnectAll();
  assert.deepStrictEqual(mgr.knownUrls, []);
});

test('boot reports an expired session as such — one warning, one clickable toast', () => {
  const src = readFileSync(new URL('../js/console/stencilApi.js', import.meta.url), 'utf8');
  const boot = src.slice(src.indexOf('if (firstInit && getAutoConnect())'), src.indexOf('let peers = []'));
  // Settled, so a dead token can never surface as an unhandled rejection…
  assert.ok(boot.includes('Promise.allSettled('), 'boot never leaves a rejection loose');
  // …counted apart from unreachable servers, because the cures differ.
  assert.ok(boot.includes('r.reason?.expired'));
  assert.match(boot, /Session expired on \$\{expired\.length\} saved server/);
  // A count says nothing about WHICH server to go check — one toast per address instead.
  assert.ok(boot.includes("notify(`Couldn't reach ${url}`, 'info')"));
  assert.ok(!/Couldn't reach \$\{unreachable\.length\}/.test(boot), 'not a count');
  // One diagnostic line PER CASE (a known-dead session adopted at boot, or one that
  // turns out dead now) — and both are warnings, never errors, never a raw rejection.
  assert.strictEqual(boot.split('console.').length - 1, 2, 'one line per case, no more');
  assert.strictEqual(boot.split('console.warn(').length - 1, 2);
  assert.ok(!/console\.(error|log)\(/.test(boot));
  // The toast is the way in: it opens Connections, where the row offers Reconnect.
  assert.ok(boot.includes("onClick: () => document.getElementById('connect-btn')?.click()"));
});

test('the connections modal shows expired rows with a labelled Reconnect', () => {
  const src = readFileSync(new URL('../js/ui/connectModal.js', import.meta.url), 'utf8');
  assert.ok(src.includes('const known = cm ? cm.knownUrls : []'), 'expired rows are listed too');
  assert.match(src, /expired: 'Session expired — reconnect to sign in again'/);
  assert.ok(src.includes("row.classList.add('connect-expired')"));
  // Mint first, then ask for a token — and the token may be the ADMIN one.
  assert.ok(src.includes("mgr().reconnectOne(url, expired ? '' : undefined)"));
  assert.ok(src.includes('isExpiredSession(err)'));
  assert.match(src, /admin token, which mints a fresh session/);
  assert.ok(src.includes("mgr().reconnectOne(url, String(token).trim())"));
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  assert.match(css, /\.conn-status-expired \{ background: #e0a800/, 'amber, not the red of a dead server');
  // The labelled button wears that amber as a FILL: every other row button is
  // accent-filled, so an amber outline on an accent face was unreadable.
  assert.match(css, /\.connect-row\.connect-expired \.connect-reconnect-one:not\(:disabled\) \{[^}]*background: #e0a800/,
    'the row’s Reconnect is amber-filled to match the row it fixes');
});

// ── The dead credential must not cost a request on every boot ───────────────
// Reported after the first fix: the row and the toast were right, but the app still
// spent a doomed /projects call — and Chrome's own red 401 — on every single load.
test('a refused credential is remembered as refused, and adopted without a request', async () => {
  const server = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'stale' }).catch(() => {});
  // What gets persisted now carries the refusal…
  assert.deepStrictEqual(mgr.snapshot(), [{ url: 'http://a:1', token: 'stale', expired: true }]);
  // …and the next boot adopts it with ZERO requests, keeping the row and its action.
  const next = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  const before = server.state.calls.length;
  next.adoptExpired({ url: 'http://a:1', token: 'stale' });
  assert.strictEqual(server.state.calls.length, before, 'not one request');
  assert.deepStrictEqual(next.expiredUrls, ['http://a:1']);
  assert.strictEqual(next.get('http://a:1').status, 'expired');
  assert.strictEqual(next.reconnectable, true, 'and it is still actionable');
  // Adopting twice, or over a live connection, is a no-op.
  next.adoptExpired({ url: 'http://a:1', token: 'stale' });
  assert.deepStrictEqual(next.expiredUrls, ['http://a:1']);
  // Signing in again clears the flag, so it stops being persisted as dead — and the
  // credential's KIND is now known, so the next connect skips the doomed probe.
  await next.reconnectOne('http://a:1', 'admin-token');
  assert.deepStrictEqual(next.snapshot(), [{ url: 'http://a:1', token: 'admin-token', kind: 'admin' }]);
});

test('a live connection to the same url repairs the expired record', async () => {
  const { fetchImpl } = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'stale' }).catch(() => {});
  assert.deepStrictEqual(mgr.expiredUrls, ['http://a:1']);
  // The user connects that SAME server with a working credential (the connect form):
  // the dead record must not linger beside it — one url, one row.
  await mgr.connect({ url: 'http://a:1', token: 'issued-token' });
  assert.deepStrictEqual(mgr.expiredUrls, [], 'the expired record is repaired away');
  assert.deepStrictEqual(mgr.knownUrls, ['http://a:1'], 'and the modal shows ONE row');
  assert.deepStrictEqual(mgr.snapshot(), [{ url: 'http://a:1', token: 'issued-token' }]);
});

test('boot adopts known-dead sessions instead of re-requesting them', () => {
  const src = readFileSync(new URL('../js/console/stencilApi.js', import.meta.url), 'utf8');
  const boot = src.slice(src.indexOf('if (firstInit && getAutoConnect())'), src.indexOf('let peers = []'));
  assert.ok(boot.includes('const dead = saved.filter((s) => s.expired)'));
  assert.ok(boot.includes('connMgr.adoptExpired(s)'), 'no request for a credential already refused');
  assert.ok(boot.includes('Promise.allSettled(live.map((s) => connMgr.connect(s)))'), 'the rest still connect');
  // The saved shape keeps the flag, or the next boot would forget and retry.
  const store = readFileSync(new URL('../js/net/connectionStore.js', import.meta.url), 'utf8');
  assert.match(store, /if \(s\.expired\) out\.expired = true;/);
  assert.match(store, /if \(s\.kind === 'admin'\) out\.kind = 'admin';/, 'the credential kind rides along too');
});

test('the Servers button carries the needs-re-auth dot without opening the modal', () => {
  const src = readFileSync(new URL('../js/ui/connectModal.js', import.meta.url), 'utf8');
  assert.ok(src.includes('const syncExpiredBadge = ()'));
  assert.ok(src.includes("el?.classList?.toggle('conn-needs-auth', on)"));
  assert.match(src, /a saved session expired, reconnect to sign in again/, 'the tooltip says what to do');
  // Runs at wire time AND on every connections change, and covers the fullscreen clone.
  assert.ok(src.includes("document.querySelectorAll('#fs-controls-panel #connect-btn')"));
  const at = src.indexOf('syncExpiredBadge();');
  assert.ok(at > -1 && at < src.indexOf("window.addEventListener('stencil:connections-changed'"),
    'the badge is correct before any event fires');
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  const dot = css.slice(css.indexOf('#connect-btn.conn-needs-auth::before'), css.indexOf('/* #zoom-fit gets'));
  assert.match(dot, /position: absolute/);
  assert.match(dot, /background: #e0a800/, 'amber, like the row — the server is up');
  assert.ok(!/margin|padding/.test(dot), 'nothing that could shift the toolbar');
});

// ── The admin credential's doomed first request ─────────────────────────────
// Reported: every load showed `GET /projects 401 → POST /auth/token 200 → GET /projects
// 200`. The saved credential is an ADMIN token: it cannot list projects, so probing it
// as a session token is guaranteed to fail before the mint rescues it. Remember what the
// credential IS and the 401 stops happening at all.
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
