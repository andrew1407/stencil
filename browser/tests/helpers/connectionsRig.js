// Shared rig for the connections.test.js family: an in-memory server model behind a mock
// fetch (the subset of the REST surface the client uses) and a no-op WebSocket.
import { ConnectionManager } from '../../js/net/connectionManager.js';

// ── A mock fetch backed by an in-memory server model ──
// Routes the subset of the REST surface the client uses.
export const makeMockServer = (opts = {}) => {
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
export class StubWS {
  constructor(url) { this.url = url; this._l = {}; StubWS.last = this; }
  addEventListener(t, cb) { (this._l[t] ||= []).push(cb); }
  send() {}
  close() { (this._l.close || []).forEach((cb) => cb()); }
  fire(t, data) { (this._l[t] || []).forEach((cb) => cb(data)); }
}

// The app surface createStencil touches.
export const facadeApp = (server, extra = {}) => {
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

// A saved token the server no longer knows must leave a row and a message, not one red 401 in the
// console; requireToken is what the mock MINTS, so an admin-token round genuinely revives it.
export const deadSessionServer = () => makeMockServer({ requireToken: 'issued-token', adminToken: 'admin-token' });
