// ── Stencil server connections (client side) ────────────────────────────────
// Each ServerConnection owns a token, a live /ws events feed, and the REST surface
// (server/internal/protocol); fetch + WebSocket are injected for `node --test`.
import { Emitter } from '../core/emitter.js';

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

// The two statuses that mean "your credential was refused" rather than "the server is
// not there": a saved token the server has forgotten (restart, expiry, revocation).
export const isAuthStatus = (status) => status === 401 || status === 403;
// …and the same question asked of a thrown REST error, wherever one surfaces (the chat
// provider hits it on /llm/chat exactly as the projects list does on /projects).
export const isExpiredSession = (err) => !!err && (err.expired === true || isAuthStatus(err.status));

// A single connected server.
export class ServerConnection {
  constructor(url, { token = '', kind = '', fetchImpl, WebSocketImpl, clientId } = {}) {
    this.url = normalizeUrl(url);
    this.token = token;
    // What the user supplied, kept for persistence/reconnect: a session token
    // minted FROM it dies with the server, the credential can always mint anew.
    this.credential = token;
    // …and WHAT it is. An admin token cannot list projects, so probing it as a session
    // token always 401s first; once the mint round proves it, the kind is remembered and
    // later connects go straight to minting. '' = not yet known: probe first.
    this.credentialKind = kind === 'admin' ? 'admin' : '';
    this._fetch = fetchImpl || globalThis.fetch?.bind(globalThis);
    this._WS = WebSocketImpl || globalThis.WebSocket;
    this.clientId = clientId || ('c_' + Math.random().toString(36).slice(2, 10));
    this._events = null;       // events-feed socket
    this._bus = new Emitter(); // 'event' channel: live project-event messages
    this.connected = false;
    this._closing = false;
    // UI-dot status: 'connecting'|'connected'|'error'|'expired'; _onStatus (set by
    // ConnectionManager) re-renders the connections UI on change. 'expired' is its own
    // state on purpose: the server is up, this SESSION is dead — only a new token helps.
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
    let authFailed = false;
    try {
      if (!this.token) {
        const r = await this._req('POST', '/auth/token', { body: {} });
        this.token = r.token;
      } else if (this.credentialKind === 'admin') {
        // Known admin credential: mint straight away, no doomed probe. If the server
        // has since stopped accepting it, this throws and lands in the expired state
        // below exactly like any other refusal.
        const r = await this._req('POST', '/auth/token', { body: {} });
        authFailed = true;                 // …until /projects proves the session works
        this.token = r.token;
        await this._req('GET', '/projects');
        authFailed = false;
      } else {
        try {
          await this._req('GET', '/projects'); // validate
        } catch (err) {
          // Desktop parity: the pasted value may be the server's ADMIN token —
          // it can't list projects, but it can MINT a session token. The same
          // round rescues a saved SESSION token the server has since forgotten,
          // on any server that mints without a credential.
          if (!isAuthStatus(err.status)) throw err;
          authFailed = true;
          const r = await this._req('POST', '/auth/token', { body: {} });
          this.token = r.token;
          // The credential is NOT replaced: it may be the admin token, which mints anew
          // every time. Only when this mint ALSO fails is the session truly over —
          // which is what the expired state below is for.
          await this._req('GET', '/projects');
          authFailed = false;
          // It minted AND the session works: this credential is an admin token. Recorded
          // (and persisted by snapshot) so the next connect skips the probe entirely.
          this.credentialKind = 'admin';
        }
      }
    } catch (err) {
      this.connected = false;
      // A rejected credential is NOT an unreachable server: the session is simply over.
      // Marked distinctly so the UI can offer the one thing that helps (a new token)
      // instead of a reconnect that will fail identically for as long as it is retried.
      const expired = authFailed || isAuthStatus(err.status);
      if (expired) err.expired = true;
      this._setStatus(expired ? 'expired' : 'error');
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

  // Delete one filestore-only kind (video/variantN/chat — server answers 204,
  // idempotently; original/result are refused server-side).
  async deleteFile(id, kind) {
    return this._req('DELETE', `/projects/${encodeURIComponent(id)}/files/${kind}`);
  }

  // Fetch raw image bytes (authenticated) as a Blob, for opening a remote project.
  async fetchFile(id, kind) {
    const resp = await this._req('GET', `/projects/${encodeURIComponent(id)}/files/${kind}`, { raw: true });
    return resp.blob();
  }

  // Stamp a remote project record so the UI can distinguish/route it.
  tagRemote(p) { return { ...p, [REMOTE_FLAG]: true, serverUrl: this.url }; }

  // ── live events feed (project created/updated/deleted) ──
  onEvent(cb) { return this._bus.on('event', cb); }

  _emit(msg) { this._bus.emit('event', msg, this); }

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
    // url -> ServerConnection whose credential the server REFUSED. Kept out of the live
    // set (nothing may try to use one) but remembered, so the UI can still show the row,
    // keep its saved URL, and offer the one action that helps: a new token.
    this._expired = new Map();
    this._lastSet = [];        // for reconnect()
  }

  get urls() { return Array.from(this._conns.keys()); }
  get expiredUrls() { return Array.from(this._expired.keys()); }
  // Everything the user has a row for: live first, then the sessions that need a token.
  get knownUrls() { return [...this._conns.keys(), ...this._expired.keys()]; }
  isExpired(url) { try { return this._expired.has(normalizeUrl(url)); } catch { return false; } }
  get connections() { return Array.from(this._conns.values()); }
  // True when reconnect() has anything to act on (live set or the last-known one).
  get reconnectable() { return this._conns.size > 0 || this._expired.size > 0 || this._lastSet.length > 0; }
  // Persistable view of the live set: [{ url, token }] (see connectionStore.js). The
  // ORIGINAL credential is what persists — a minted session token would die with the
  // server. Expired sessions are included: the saved URL must survive a dead token.
  snapshot() {
    return [
      ...this.connections.map((c) => (c.credentialKind === 'admin'
        ? { url: c.url, token: c.credential || '', kind: 'admin' }
        : { url: c.url, token: c.credential || '' })),
      // The refusal is persisted with it: a credential the server rejected will never be
      // accepted again, so the next boot must not spend a request (and a console 401) on
      // it. The row still appears, still offers Reconnect — it just costs nothing.
      ...[...this._expired.values()].map((c) => ({ url: c.url, token: c.credential || '', expired: true })),
    ];
  }

  // Take a SAVED entry already known to be refused straight into the expired set, with
  // no request at all. Boot uses it for the flag above.
  adoptExpired({ url, token = '', kind = '' } = {}) {
    const norm = normalizeUrl(url);
    if (this._conns.has(norm) || this._expired.has(norm)) return this;
    const conn = new ServerConnection(norm, { token, kind, fetchImpl: this._fetch, WebSocketImpl: this._WS });
    conn._onStatus = () => this._onChange({ type: 'status', connection: conn });
    conn._setStatus('expired');
    this._expired.set(norm, conn);
    this._onChange({ type: 'expired', connection: conn });
    return this;
  }
  // has() answers "is there a USABLE connection here" — an expired one is not.
  has(url) { return this._conns.has(normalizeUrl(url)); }
  // get() answers "what do I know about this url", expired sessions included, so the UI
  // can render their status without a second lookup path.
  get(url) {
    const norm = normalizeUrl(url);
    return this._conns.get(norm) || this._expired.get(norm) || null;
  }
  get last() { const u = this.urls; return u.length ? this._conns.get(u[u.length - 1]) : null; }

  // Connect one or more servers. Accepts a URL string, {url, token}, or an array
  // of either. Resolves once all are connected (already-connected urls are no-ops).
  async connect(spec) {
    const items = Array.isArray(spec) ? spec : [spec];
    for (const item of items) {
      const { url, token, kind } = typeof item === 'string' ? { url: item, token: '' } : (item || {});
      const norm = normalizeUrl(url);
      if (this._conns.has(norm)) continue;
      const conn = new ServerConnection(norm, {
        token, kind, fetchImpl: this._fetch, WebSocketImpl: this._WS,
      });
      // Re-render the connections UI whenever this connection's status changes
      // (connecting → connected, or an unexpected drop → error).
      conn._onStatus = () => this._onChange({ type: 'status', connection: conn });
      try {
        await conn.handshake();
      } catch (err) {
        // A refused credential is remembered, not retried: the row stays in the UI as
        // "session expired" with its URL intact, and nothing tries this token again.
        // Any other failure (server down) may recover — reconnect-all is the answer.
        if (err.expired) {
          this._expired.set(norm, conn);
          this._onChange({ type: 'expired', connection: conn });
        }
        throw err;
      }
      this._expired.delete(norm);
      conn.onEvent((msg, c) => this._onChange({ type: 'event', message: msg, connection: c }));
      this._conns.set(norm, conn);
    }
    this._lastSet = this.snapshot();
    this._onChange({ type: 'connect' });
    return this;
  }

  // Disconnect a specific url (or, if omitted, the most recently added).
  disconnect(url) {
    let target;
    if (url == null) { const k = this.knownUrls; target = k[k.length - 1]; }
    else target = normalizeUrl(url);
    const conn = target && (this._conns.get(target) || this._expired.get(target));
    if (conn) { conn.close(); this._conns.delete(target); this._expired.delete(target); }
    this._onChange({ type: 'disconnect' });
    return this;
  }

  disconnectAll() {
    for (const c of this._conns.values()) c.close();
    for (const c of this._expired.values()) c.close();
    this._conns.clear();
    this._expired.clear();
    this._onChange({ type: 'disconnect' });
    return this;
  }

  // Reorder the live connection set to match `orderedUrls`. The Map has no reorder, so
  // rebuild by re-inserting in the desired order (unknown urls skipped, missing ones keep
  // relative order at the end). Routes through _onChange so the new order persists and
  // broadcasts like every other mutation; _lastSet updates so reconnect() preserves it.
  reorder(orderedUrls) {
    const next = new Map();
    for (const u of orderedUrls || []) {
      let norm; try { norm = normalizeUrl(u); } catch { continue; }
      if (this._conns.has(norm) && !next.has(norm)) next.set(norm, this._conns.get(norm));
    }
    for (const [k, v] of this._conns) if (!next.has(k)) next.set(k, v);
    this._conns = next;
    this._lastSet = this.snapshot();
    this._onChange({ type: 'reorder' });
    return this;
  }

  // Re-establish a single connection (re-validating/re-issuing its token), e.g. from
  // a per-row "reconnect" button after a server blip. No-op for an unknown url.
  // `token` overrides the stored credential — the "session expired, here is a new token"
  // path (empty string = mint a fresh session, which is all an open server needs).
  async reconnectOne(url, token) {
    const norm = normalizeUrl(url);
    const conn = this._conns.get(norm) || this._expired.get(norm);
    const reuse = token === undefined;
    const cred = reuse ? (conn ? conn.credential : '') : token;
    // A pasted token is of unknown kind and probes first; a REUSED one keeps what we
    // learned about it, so a known admin credential never probes again.
    const kind = reuse ? (conn?.credentialKind || '') : '';
    if (conn) {
      conn.close();
      this._conns.delete(norm);
      this._expired.delete(norm);
    }
    await this.connect(cred ? { url: norm, token: cred, kind } : norm);
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
