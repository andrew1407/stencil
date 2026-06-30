// ── Stencil server connections (client side) ────────────────────────────────
// Each ServerConnection owns a token, a live /ws events feed, and the REST surface
// (server/internal/protocol); fetch + WebSocket are injected for `node --test`.

// Remote project ids are namespaced so they never collide with local base36 ids;
// each remote meta carries `serverUrl` with `remote: true` (golden outline in the UI).
export const REMOTE_FLAG = 'remote';

// True for a loopback host (localhost, *.localhost, 127.0.0.0/8, ::1), where plaintext
// http is safe because the bytes never leave the machine.
export const isLoopbackHost = (host) => {
  if (!host) return false;
  const h = host.toLowerCase().replace(/^\[|\]$/g, ''); // strip any IPv6 brackets
  if (h === 'localhost' || h.endsWith('.localhost')) return true;
  if (h === '::1') return true;
  return /^127\.\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(h);
};

// normalizeUrl turns 'host:8090' / 'http://host:8090/' into a clean origin. Secure by
// default: a bare REMOTE host gets https; loopback keeps http (dev servers run plaintext
// on localhost). An explicit scheme is preserved — the user opts into cleartext.
export const normalizeUrl = (raw) => {
  let s = String(raw == null ? '' : raw).trim();
  if (!s) throw new Error('Server URL is required');
  if (!/^https?:\/\//i.test(s)) {
    const host = new URL('http://' + s).hostname;
    s = (isLoopbackHost(host) ? 'http://' : 'https://') + s;
  }
  const u = new URL(s);
  return u.origin;
};

// True when `origin` would send the bearer token + image bytes in CLEARTEXT to a remote
// host (http scheme, not loopback) — the UI warns on these.
export const isInsecureRemote = (origin) => {
  try {
    const u = new URL(origin);
    return u.protocol === 'http:' && !isLoopbackHost(u.hostname);
  } catch { return false; }
};

// wsUrl derives the WebSocket endpoint for an http(s) origin.
export const wsUrl = (origin) => origin.replace(/^http/i, 'ws') + '/ws';

// A single connected server.
export class ServerConnection {
  constructor(url, { token = '', fetchImpl, WebSocketImpl, clientId } = {}) {
    this.url = normalizeUrl(url);
    this.token = token;
    this._fetch = fetchImpl || globalThis.fetch?.bind(globalThis);
    this._WS = WebSocketImpl || globalThis.WebSocket;
    this.clientId = clientId || ('c_' + Math.random().toString(36).slice(2, 10));
    this._events = null;       // events-feed socket
    this._eventCbs = new Set();
    this.connected = false;
    this._closing = false;
    // UI-dot status: 'connecting'|'connected'|'error'; _onStatus (set by ConnectionManager)
    // re-renders the connections UI on change.
    this.status = 'connecting';
    this._onStatus = null;
  }

  _setStatus(s) {
    if (this.status === s) return;
    this.status = s;
    try { this._onStatus && this._onStatus(this); } catch { /* listener error */ }
  }

  // ── REST ──
  async _req(method, path, { body, raw, query } = {}) {
    if (!this._fetch) throw new Error('no fetch implementation available');
    let url = this.url + path;
    if (query) url += '?' + new URLSearchParams(query).toString();
    const headers = { Authorization: 'Bearer ' + this.token };
    let payload = body;
    if (body != null && !raw) {
      headers['Content-Type'] = 'application/json';
      payload = JSON.stringify(body);
    }
    const resp = await this._fetch(url, { method, headers, body: payload });
    if (!resp.ok) {
      let msg = `HTTP ${resp.status}`;
      try { const e = await resp.json(); if (e && e.message) msg = e.message; } catch { /* non-JSON */ }
      const err = new Error(`${method} ${path}: ${msg}`);
      err.status = resp.status;
      throw err;
    }
    if (resp.status === 204) return null;
    if (raw) return resp;
    return resp.json();
  }

  // Acquire/validate a token, then verify access by listing projects.
  async handshake() {
    this._setStatus('connecting');
    try {
      if (!this.token) {
        const r = await this._req('POST', '/auth/token', { body: {} });
        this.token = r.token;
      } else {
        await this._req('GET', '/projects'); // validate
      }
    } catch (err) {
      this.connected = false;
      this._setStatus('error');
      throw err;
    }
    this.connected = true;
    this._setStatus('connected');
    this._openEvents();
    return this;
  }

  async listProjects() {
    const r = await this._req('GET', '/projects');
    return (r.projects || []).map((p) => this.tagRemote(p));
  }

  async getProject(id) { return this._req('GET', `/projects/${encodeURIComponent(id)}`); }

  async createProject(body) { return this.tagRemote(await this._req('POST', '/projects', { body })); }

  async updateProject(id, body) { return this.tagRemote(await this._req('PUT', `/projects/${encodeURIComponent(id)}`, { body })); }

  async deleteProject(id) { return this._req('DELETE', `/projects/${encodeURIComponent(id)}`); }

  // Upload raw image bytes; the server is codec-free so dimensions are passed in.
  async putFile(id, kind, bytes, { ext = 'png', w = 0, h = 0 } = {}) {
    return this._req('POST', `/projects/${encodeURIComponent(id)}/files/${kind}`, {
      body: bytes, raw: true, query: { ext, w: String(w), h: String(h) },
    });
  }

  fileUrl(id, kind) { return `${this.url}/projects/${encodeURIComponent(id)}/files/${kind}`; }

  // Fetch raw image bytes (authenticated) as a Blob, for opening a remote project.
  async fetchFile(id, kind) {
    const resp = await this._req('GET', `/projects/${encodeURIComponent(id)}/files/${kind}`, { raw: true });
    return resp.blob();
  }

  // Stamp a remote project record so the UI can distinguish/route it.
  tagRemote(p) { return { ...p, [REMOTE_FLAG]: true, serverUrl: this.url }; }

  // ── live events feed (project created/updated/deleted) ──
  onEvent(cb) { this._eventCbs.add(cb); return () => this._eventCbs.delete(cb); }

  _emit(msg) { for (const cb of this._eventCbs) { try { cb(msg, this); } catch { /* listener error */ } } }

  _openEvents() {
    if (!this._WS) return; // no WebSocket (e.g. some test envs) — REST still works
    try {
      const ws = new this._WS(wsUrl(this.url));
      this._events = ws;
      ws.addEventListener('open', () => {
        ws.send(JSON.stringify({ type: 'hello', token: this.token, clientId: this.clientId }));
      });
      ws.addEventListener('message', (ev) => {
        let msg; try { msg = JSON.parse(ev.data); } catch { return; }
        if (msg.type === 'project-event') this._emit(msg);
      });
      ws.addEventListener('close', () => {
        this._events = null;
        // An unexpected drop (not a user disconnect) → the live feed is gone; show red.
        if (!this._closing) { this.connected = false; this._setStatus('error'); }
      });
    } catch { /* events are best-effort; REST keeps working */ }
  }

  close() {
    this._closing = true;
    this.connected = false;
    this._setStatus('disconnected');
    try { this._events?.close(); } catch { /* already closed */ }
    this._events = null;
  }
}

// Manages the set of connected servers for one editor session.
export class ConnectionManager {
  constructor({ fetchImpl, WebSocketImpl, onChange } = {}) {
    this._fetch = fetchImpl;
    this._WS = WebSocketImpl;
    this._onChange = onChange || (() => {});
    this._conns = new Map();   // url -> ServerConnection
    this._lastSet = [];        // for reconnect()
  }

  get urls() { return Array.from(this._conns.keys()); }
  get connections() { return Array.from(this._conns.values()); }
  // Persistable view of the live set: [{ url, token }] (see connectionStore.js).
  snapshot() { return this.connections.map((c) => ({ url: c.url, token: c.token || '' })); }
  has(url) { return this._conns.has(normalizeUrl(url)); }
  get(url) { return this._conns.get(normalizeUrl(url)) || null; }
  get last() { const u = this.urls; return u.length ? this._conns.get(u[u.length - 1]) : null; }

  // Connect one or more servers. Accepts a URL string, {url, token}, or an array
  // of either. Resolves once all are connected (already-connected urls are no-ops).
  async connect(spec) {
    const items = Array.isArray(spec) ? spec : [spec];
    for (const item of items) {
      const { url, token } = typeof item === 'string' ? { url: item, token: '' } : (item || {});
      const norm = normalizeUrl(url);
      if (this._conns.has(norm)) continue;
      const conn = new ServerConnection(norm, {
        token, fetchImpl: this._fetch, WebSocketImpl: this._WS,
      });
      // Re-render the connections UI whenever this connection's status changes
      // (connecting → connected, or an unexpected drop → error).
      conn._onStatus = () => this._onChange({ type: 'status', connection: conn });
      await conn.handshake();
      conn.onEvent((msg, c) => this._onChange({ type: 'event', message: msg, connection: c }));
      this._conns.set(norm, conn);
    }
    this._lastSet = this.urls.slice();
    this._onChange({ type: 'connect' });
    return this;
  }

  // Disconnect a specific url (or, if omitted, the most recently added).
  disconnect(url) {
    let target;
    if (url == null) target = this.urls[this.urls.length - 1];
    else target = normalizeUrl(url);
    const conn = target && this._conns.get(target);
    if (conn) { conn.close(); this._conns.delete(target); }
    this._onChange({ type: 'disconnect' });
    return this;
  }

  disconnectAll() {
    for (const c of this._conns.values()) c.close();
    this._conns.clear();
    this._onChange({ type: 'disconnect' });
    return this;
  }

  // Re-establish a single connection (re-validating/re-issuing its token), e.g. from
  // a per-row "reconnect" button after a server blip. No-op for an unknown url.
  async reconnectOne(url) {
    const norm = normalizeUrl(url);
    const conn = this._conns.get(norm);
    const token = conn ? conn.token : '';
    if (conn) {
      conn.close();
      this._conns.delete(norm);
    }
    await this.connect(token ? { url: norm, token } : norm);
    return this;
  }

  // Re-establish the last connected set (tokens are re-validated/re-issued).
  async reconnect() {
    const set = this._lastSet.slice();
    this.disconnectAll();
    if (set.length) await this.connect(set);
    return this;
  }

  // Aggregate remote projects across every connection (for the projects modal).
  async remoteProjects() {
    const out = [];
    await Promise.all(this.connections.map(async (c) => {
      try { out.push(...await c.listProjects()); } catch { /* skip unreachable server */ }
    }));
    return out;
  }
}
