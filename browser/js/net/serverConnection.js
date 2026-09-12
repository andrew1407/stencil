// One connected server: a token, a live /ws feed and the REST surface
// (server/internal/protocol); fetch + WebSocket are injected for `node --test`.
import { Emitter } from '../core/emitter.js';
import { timeoutSignal } from './abortable.js';
import { REMOTE_FLAG, normalizeUrl, buildInviteUrl, wsUrl, isAuthStatus } from './urlRules.js';

export class ServerConnection {
  #fetch; #WS;
  #events = null;       // events-feed socket
  #bus = new Emitter(); // 'event' channel: live project-event messages
  #closing = false;
  constructor(url, { token = '', kind = '', fetchImpl, WebSocketImpl, clientId } = {}) {
    this.url = normalizeUrl(url);
    this.token = token;
    // What the user supplied: a session token minted FROM it dies with the server.
    this.credential = token;
    // An admin token cannot list projects, so probing it as a session token always 401s;
    // once proven the kind is remembered. '' = not yet known: probe first.
    this.credentialKind = kind === 'admin' ? 'admin' : '';
    this.#fetch = fetchImpl ?? globalThis.fetch?.bind(globalThis);
    this.#WS = WebSocketImpl ?? globalThis.WebSocket;
    this.clientId = clientId || ('c_' + Math.random().toString(36).slice(2, 10));
    this.connected = false;
    // 'expired' is its own state: the server is up, this SESSION is dead — only a new token helps.
    this.status = 'connecting';
    this._onStatus = null;
  }

  _setStatus(s) {
    if (this.status === s) return;
    this.status = s;
    try { this._onStatus && this._onStatus(this); } catch { /* listener error */ }
  }

  // `token` overrides the bearer for one request; `retried` marks the single re-mint retry.
  async #req(method, path, { body, raw, query, token, retried = false } = {}) {
    if (!this.#fetch) throw new Error('no fetch implementation available');
    let url = this.url + path;
    if (query) url += '?' + new URLSearchParams(query).toString();
    const headers = { Authorization: 'Bearer ' + (token ?? this.token) };
    let payload = body;
    if (body != null && !raw) {
      headers['Content-Type'] = 'application/json';
      payload = JSON.stringify(body);
    }
    const resp = await this.#fetch(url, { method, headers, body: payload, signal: timeoutSignal() });
    if (!resp.ok) {
      // A minted session token dies with a server restart: re-mint from the credential once
      // and retry in place (extension parity: connections.js req()).
      if (!retried && isAuthStatus(resp.status) && this.credential && path !== '/auth/token') {
        let r;
        try {
          r = await this.#req('POST', '/auth/token', { body: {}, token: this.credential, retried: true });
        } catch (err) {
          // The credential no longer mints either: this session is truly over.
          if (isAuthStatus(err.status)) { err.expired = true; this.connected = false; this._setStatus('expired'); }
          throw err;
        }
        this.token = r.token;
        const out = await this.#req(method, path, { body, raw, query, retried: true });
        // It minted AND the session works: the same conclusion handshake() records.
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
        const r = await this.#req('POST', '/auth/token', { body: {} });
        this.token = r.token;
      } else if (this.credentialKind === 'admin') {
        // Known admin credential: mint straight away, no doomed probe.
        const r = await this.#req('POST', '/auth/token', { body: {} });
        authFailed = true;                 // …until /projects proves the session works
        this.token = r.token;
        await this.#req('GET', '/projects', { retried: true });
        authFailed = false;
      } else {
        try {
          // Connect-time refusals are handled here, not by #req's mid-session re-mint.
          await this.#req('GET', '/projects', { retried: true });
        } catch (err) {
          // Desktop parity: the pasted value may be the ADMIN token — it cannot list projects but
          // can MINT a session token; the same round rescues a forgotten session token.
          if (!isAuthStatus(err.status)) throw err;
          authFailed = true;
          const r = await this.#req('POST', '/auth/token', { body: {} });
          this.token = r.token;
          // The credential is NOT replaced: it may be the admin token, which mints anew each time.
          await this.#req('GET', '/projects', { retried: true });
          authFailed = false;
          // Minted AND works: an admin token, recorded so the next connect skips the probe.
          this.credentialKind = 'admin';
        }
      }
    } catch (err) {
      this.connected = false;
      // A rejected credential is NOT an unreachable server: marked distinctly so the UI offers
      // a new token instead of a reconnect that fails identically.
      const expired = authFailed || isAuthStatus(err.status);
      if (expired) err.expired = true;
      this._setStatus(expired ? 'expired' : 'error');
      throw err;
    }
    this.connected = true;
    this._setStatus('connected');
    this.#openEvents();
    return this;
  }

  async listProjects() {
    const r = await this.#req('GET', '/projects');
    return (r.projects || []).map((p) => this.tagRemote(p));
  }

  async getProject(id) { return this.#req('GET', `/projects/${encodeURIComponent(id)}`); }

  async createProject(body) { return this.tagRemote(await this.#req('POST', '/projects', { body })); }

  async updateProject(id, body) { return this.tagRemote(await this.#req('PUT', `/projects/${encodeURIComponent(id)}`, { body })); }

  async deleteProject(id) { return this.#req('DELETE', `/projects/${encodeURIComponent(id)}`); }

  // Upload raw image bytes; the server is codec-free so dimensions are passed in.
  async putFile(id, kind, bytes, { ext = 'png', w = 0, h = 0 } = {}) {
    return this.#req('POST', `/projects/${encodeURIComponent(id)}/files/${kind}`, {
      body: bytes, raw: true, query: { ext, w: String(w), h: String(h) },
    });
  }

  fileUrl(id, kind) { return `${this.url}/projects/${encodeURIComponent(id)}/files/${kind}`; }

  // video/variantN/chat only (204, idempotent); original/result are refused server-side.
  async deleteFile(id, kind) {
    return this.#req('DELETE', `/projects/${encodeURIComponent(id)}/files/${kind}`);
  }

  async fetchFile(id, kind) {
    const resp = await this.#req('GET', `/projects/${encodeURIComponent(id)}/files/${kind}`, { raw: true });
    return resp.blob();
  }

  // A FRESH session token from this connection's credential, as `<url>#token=<token>`.
  async mintInvite() {
    const r = await this.#req('POST', '/auth/token', {
      body: { label: 'invite' }, token: this.credential, retried: true,
    });
    return buildInviteUrl(this.url, r.token);
  }

  tagRemote(p) { return { ...p, [REMOTE_FLAG]: true, serverUrl: this.url }; }

  onEvent(cb) { return this.#bus.on('event', cb); }

  #emit(msg) { this.#bus.emit('event', msg, this); }

  #openEvents() {
    if (!this.#WS) return; // no WebSocket (e.g. some test envs) — REST still works
    try {
      const ws = new this.#WS(wsUrl(this.url));
      this.#events = ws;
      ws.addEventListener('open', () => {
        ws.send(JSON.stringify({ type: 'hello', token: this.token, clientId: this.clientId }));
      });
      ws.addEventListener('message', (ev) => {
        let msg; try { msg = JSON.parse(ev.data); } catch { return; }
        if (msg.type === 'project-event') this.#emit(msg);
      });
      ws.addEventListener('close', () => {
        this.#events = null;
        // An unexpected drop (not a user disconnect) → the live feed is gone; show red.
        if (!this.#closing) { this.connected = false; this._setStatus('error'); }
      });
    } catch { /* events are best-effort; REST keeps working */ }
  }

  close() {
    this.#closing = true;
    this.connected = false;
    this._setStatus('disconnected');
    try { this.#events?.close(); } catch { /* already closed */ }
    this.#events = null;
  }
}
