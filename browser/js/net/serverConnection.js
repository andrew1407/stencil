// ── One connected Stencil server ────────────────────────────────────────────
// Owns a token, a live /ws events feed and the REST surface (server/internal/protocol);
// fetch + WebSocket are injected so `node --test` can drive it without either.
import { Emitter } from '../core/emitter.js';
import { timeoutSignal } from './abortable.js';
import { REMOTE_FLAG, normalizeUrl, buildInviteUrl, wsUrl, isAuthStatus } from './urlRules.js';

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
  // `token` overrides the bearer for one request; `retried` marks the single
  // re-mint retry (and handshake's own probes), so a refusal never mints twice.
  async _req(method, path, { body, raw, query, token, retried = false } = {}) {
    if (!this._fetch) throw new Error('no fetch implementation available');
    let url = this.url + path;
    if (query) url += '?' + new URLSearchParams(query).toString();
    const headers = { Authorization: 'Bearer ' + (token ?? this.token) };
    let payload = body;
    if (body != null && !raw) {
      headers['Content-Type'] = 'application/json';
      payload = JSON.stringify(body);
    }
    const resp = await this._fetch(url, { method, headers, body: payload, signal: timeoutSignal() });
    if (!resp.ok) {
      // A minted session token dies with a server restart — while the user's
      // credential is at hand, re-mint with it once and retry the request in
      // place (extension parity: connections.js req()).
      if (!retried && isAuthStatus(resp.status) && this.credential && path !== '/auth/token') {
        let r;
        try {
          r = await this._req('POST', '/auth/token', { body: {}, token: this.credential, retried: true });
        } catch (err) {
          // The credential no longer mints either: this session is truly over.
          if (isAuthStatus(err.status)) { err.expired = true; this.connected = false; this._setStatus('expired'); }
          throw err;
        }
        this.token = r.token;
        const out = await this._req(method, path, { body, raw, query, retried: true });
        // It minted AND the session works: the same conclusion (and probe-skip)
        // handshake() records on its own rescue round.
        this.credentialKind = 'admin';
        return out;
      }
      let msg = `HTTP ${resp.status}`;
      try { const e = await resp.json(); if (e && e.message) msg = e.message; } catch { /* non-JSON */ }
      throw Object.assign(new Error(`${method} ${path}: ${msg}`), { status: resp.status });
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
        await this._req('GET', '/projects', { retried: true });
        authFailed = false;
      } else {
        try {
          // retried: connect-time refusals are handled right here (with the
          // credentialKind bookkeeping), not by _req's mid-session re-mint.
          await this._req('GET', '/projects', { retried: true }); // validate
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
          await this._req('GET', '/projects', { retried: true });
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

  // Mint a FRESH session token from this connection's credential and wrap it into an
  // invite link (`<url>#token=<token>`) a teammate pastes into their Connect form.
  async mintInvite() {
    const r = await this._req('POST', '/auth/token', {
      body: { label: 'invite' }, token: this.credential, retried: true,
    });
    return buildInviteUrl(this.url, r.token);
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
