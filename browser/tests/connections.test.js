import { test } from 'node:test';
import assert from 'node:assert';
import {
  normalizeUrl, wsUrl, ServerConnection, ConnectionManager, REMOTE_FLAG,
  isLoopbackHost, isInsecureRemote,
} from '../js/net/connectionManager.js';
import {
  requireConnection, createRemoteProject, saveRemoteProject, shouldReloadFromEvent, CONFLICT_MESSAGE,
} from '../js/net/remoteSync.js';

// ── A fake fetch backed by an in-memory server model ──
// Routes the subset of the REST surface the client uses.
const makeFakeServer = (opts = {}) => {
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
      return json(200, { token: 'issued-token', expiresAt: 0 });
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
class FakeWS {
  constructor(url) { this.url = url; this._l = {}; FakeWS.last = this; }
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
  const { state, fetchImpl } = makeFakeServer();
  const c = new ServerConnection('host:8090', { fetchImpl, WebSocketImpl: FakeWS });
  await c.handshake();
  assert.equal(c.token, 'issued-token');
  assert.ok(c.connected);
  assert.ok(state.calls.includes('POST /auth/token'));
});

test('handshake validates a supplied token via GET /projects', async () => {
  const { state, fetchImpl } = makeFakeServer({ requireToken: 'tkn' });
  const ok = new ServerConnection('host:8090', { token: 'tkn', fetchImpl, WebSocketImpl: FakeWS });
  await ok.handshake();
  assert.ok(state.calls.includes('GET /projects'));

  const bad = new ServerConnection('host:8090', { token: 'wrong', fetchImpl, WebSocketImpl: FakeWS });
  await assert.rejects(() => bad.handshake(), /HTTP 401|bad token/);
});

test('listProjects tags every record remote with its serverUrl', async () => {
  const { fetchImpl } = makeFakeServer({ projects: [{ id: 'p_a_b', name: 'X', version: 1 }] });
  const c = new ServerConnection('http://srv:9', { token: 't', fetchImpl, WebSocketImpl: FakeWS });
  await c.handshake();
  const list = await c.listProjects();
  assert.equal(list.length, 1);
  assert.equal(list[0][REMOTE_FLAG], true);
  assert.equal(list[0].serverUrl, 'http://srv:9');
});

test('ConnectionManager connects multiple servers and dedupes', async () => {
  const a = makeFakeServer(), b = makeFakeServer();
  // Route per-host by choosing fetch on url.
  const fetchImpl = (url, init) => (new URL(url).host === 'a:1' ? a.fetchImpl : b.fetchImpl)(url, init);
  let changes = 0;
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: FakeWS, onChange: () => { changes++; } });

  await mgr.connect(['http://a:1', 'http://b:2']);
  assert.deepEqual(mgr.urls, ['http://a:1', 'http://b:2']);
  await mgr.connect('http://a:1'); // already connected → no duplicate
  assert.equal(mgr.urls.length, 2);
  assert.ok(changes >= 2);
});

test('disconnect with no arg drops the most recent connection', async () => {
  const { fetchImpl } = makeFakeServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: FakeWS });
  await mgr.connect(['http://a:1', 'http://b:2']);
  mgr.disconnect();
  assert.deepEqual(mgr.urls, ['http://a:1']);
  mgr.disconnect('http://a:1');
  assert.deepEqual(mgr.urls, []);
});

test('reconnect re-establishes the last set', async () => {
  const { fetchImpl } = makeFakeServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: FakeWS });
  await mgr.connect(['http://a:1', 'http://b:2']);
  await mgr.reconnect();
  assert.deepEqual(mgr.urls, ['http://a:1', 'http://b:2']);
});

test('snapshot exposes the live set as {url, token} for persistence', async () => {
  const { fetchImpl } = makeFakeServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: FakeWS });
  await mgr.connect({ url: 'http://a:1', token: 'tkn-a' });
  await mgr.connect('http://b:2'); // token issued by handshake → 'issued-token'
  assert.deepEqual(mgr.snapshot(), [
    { url: 'http://a:1', token: 'tkn-a' },
    { url: 'http://b:2', token: 'issued-token' },
  ]);
});

test('reconnectOne re-establishes a single connection, keeping the rest', async () => {
  const { fetchImpl } = makeFakeServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: FakeWS });
  await mgr.connect(['http://a:1', 'http://b:2']);
  await mgr.reconnectOne('http://a:1');
  assert.deepEqual(mgr.urls.sort(), ['http://a:1', 'http://b:2']);
  // unknown url just connects it fresh rather than throwing
  await mgr.reconnectOne('http://c:3');
  assert.ok(mgr.has('http://c:3'));
});

test('remoteProjects aggregates across connections and survives an unreachable one', async () => {
  const a = makeFakeServer({ projects: [{ id: 'p_a_a', name: 'A', version: 0 }] });
  const b = makeFakeServer({ projects: [{ id: 'p_b_b', name: 'B', version: 0 }] });
  const fetchImpl = (url, init) => {
    const host = new URL(url).host;
    if (host === 'down:0') throw new Error('connection refused');
    return (host === 'a:1' ? a.fetchImpl : b.fetchImpl)(url, init);
  };
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: FakeWS });
  await mgr.connect(['http://a:1', 'http://b:2']);
  // Force one connection to fail on list by swapping its fetch.
  mgr.get('http://a:1')._fetch = () => { throw new Error('boom'); };
  const list = await mgr.remoteProjects();
  assert.equal(list.length, 1);
  assert.equal(list[0].name, 'B');
});

test('events feed emits project-event messages to listeners', async () => {
  const { fetchImpl } = makeFakeServer();
  const c = new ServerConnection('http://h:1', { token: 't', fetchImpl, WebSocketImpl: FakeWS });
  await c.handshake();
  let got = null;
  c.onEvent((msg) => { got = msg; });
  FakeWS.last.fire('open');
  FakeWS.last.fire('message', { data: JSON.stringify({ type: 'project-event', event: 'created', project: { id: 'p_x_y' } }) });
  assert.equal(got.event, 'created');
  assert.equal(got.project.id, 'p_x_y');
});

// ── Facade integration: connect/disconnect/reconnect/connections ──
test('stencil facade exposes the connection surface and chains', async () => {
  const { createStencil } = await import('../js/console/stencilApi.js');
  const { fetchImpl } = makeFakeServer();
  // Minimal app stub: only what createStencil touches at construction + connect.
  const app = {
    connections: new ConnectionManager({ fetchImpl, WebSocketImpl: FakeWS }),
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
  const c = new ServerConnection(url, { token: 't', fetchImpl: server.fetchImpl, WebSocketImpl: FakeWS });
  await c.handshake();
  return c;
};

test('requireConnection validates a live connection or throws clearly', async () => {
  const server = makeFakeServer();
  const mgr = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: FakeWS });
  await mgr.connect('http://srv:8090');
  assert.equal(requireConnection(mgr, 'http://srv:8090').url, 'http://srv:8090');
  assert.throws(() => requireConnection(mgr, 'http://nope:1'), /Not connected/);
  assert.throws(() => requireConnection(null, 'x'), /No server connections/);
});

test('createRemoteProject routes to createProject + putFile(original) and tracks version', async () => {
  const server = makeFakeServer();
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
  const server = makeFakeServer();
  const conn = await connectOne(server);
  const link = await createRemoteProject(conn, { name: 'Empty' });
  assert.equal(link.version, 0);
  assert.ok(!server.state.calls.some((c) => c.includes('/files/')));
  assert.equal(server.state.projects.get(link.remoteId).hasImage, false);
});

test('saveRemoteProject routes to updateProject + putFile(result) and tracks version', async () => {
  const server = makeFakeServer();
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
  const server = makeFakeServer();
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
  const server = makeFakeServer();
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
  const server = makeFakeServer({ projects: [{ id: 'p_a_b', name: 'X', version: 0 }] });
  const conn = await connectOne(server);
  await conn.deleteProject('p_a_b');
  assert.ok(!server.state.projects.has('p_a_b'));
  assert.ok(server.state.calls.includes('DELETE /projects/p_a_b'));
});

// ── Facade: address flag threads/validates through the same create path ──
const facadeApp = (server, extra = {}) => {
  const app = {
    connections: new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: FakeWS }),
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
  const server = makeFakeServer();
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
  const server = makeFakeServer();
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
  const server = makeFakeServer();
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
  // Provide a minimal localStorage so the otherwise-inert store reads/writes.
  const mem = new Map();
  globalThis.localStorage = {
    getItem: (k) => (mem.has(k) ? mem.get(k) : null),
    setItem: (k, v) => mem.set(k, String(v)),
    removeItem: (k) => mem.delete(k),
  };
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
    delete globalThis.localStorage;
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
