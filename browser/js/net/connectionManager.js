// The set of servers one editor session is connected to, plus the expired-credential set
// the Servers UI shows as rows offering Reconnect. URL + status rules: ./urlRules.js.
import { normalizeUrl, parseInviteUrl } from './urlRules.js';
import { ServerConnection } from './serverConnection.js';

export {
  REMOTE_FLAG, isLoopbackHost, normalizeUrl, parseInviteUrl, buildInviteUrl,
  isInsecureRemote, wsUrl, isAuthStatus, isExpiredSession,
} from './urlRules.js';
export { ServerConnection } from './serverConnection.js';

export class ConnectionManager {
  constructor({ fetchImpl, WebSocketImpl, onChange } = {}) {
    this._fetch = fetchImpl;
    this._WS = WebSocketImpl;
    this._onChange = onChange || (() => {});
    this._conns = new Map();   // url -> ServerConnection
    // Refused credentials, kept out of the live set but remembered so the row keeps its URL
    // and offers a new token.
    this._expired = new Map();
    // A connection being re-established stays known, or the row (and selection bar) would
    // blink out during the handshake; the stand-in carries just what the list renders.
    this._reconnecting = new Map();   // url -> { url, status, connected, credentialKind }
    this._lastSet = [];        // for reconnect()
  }

  get urls() { return Array.from(this._conns.keys()); }
  get expiredUrls() { return Array.from(this._expired.keys()); }
  // Everything the user has a row for: live first, then the sessions that need a token.
  get knownUrls() {
    const known = [...this._conns.keys(), ...this._expired.keys()];
    if (!this._reconnecting.size) return known;
    return [...new Set([...known, ...this._reconnecting.keys()])];
  }
  isExpired(url) { try { return this._expired.has(normalizeUrl(url)); } catch { return false; } }
  get connections() { return Array.from(this._conns.values()); }
  get reconnectable() { return this._conns.size > 0 || this._expired.size > 0 || this._lastSet.length > 0; }
  // The ORIGINAL credential persists — a minted session token dies with the server.
  // Expired sessions are included: the saved URL must survive a dead token.
  snapshot() {
    return [
      ...this.connections.map((c) => (c.credentialKind === 'admin'
        ? { url: c.url, token: c.credential || '', kind: 'admin' }
        : { url: c.url, token: c.credential || '' })),
      // The refusal persists too: the next boot must not spend a request (and a 401) on it.
      ...[...this._expired.values()].map((c) => ({ url: c.url, token: c.credential || '', expired: true })),
    ];
  }

  // A saved entry already known to be refused goes straight to the expired set, no request.
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
  // Expired sessions included, so the UI renders their status without a second lookup.
  get(url) {
    const norm = normalizeUrl(url);
    // The real connection first: the settled row must win over the reconnect stand-in.
    return this._conns.get(norm) || this._expired.get(norm)
        || this._reconnecting.get(norm) || null;
  }
  get last() { const u = this.urls; return u.length ? this._conns.get(u[u.length - 1]) : null; }

  // A URL string, {url, token}, or an array of either; already-connected urls are no-ops.
  async connect(spec) {
    const items = Array.isArray(spec) ? spec : [spec];
    for (const item of items) {
      const { url, token, kind } = typeof item === 'string' ? { url: item, token: '' } : (item || {});
      // An explicitly supplied token wins over the invite fragment's.
      const inv = parseInviteUrl(url);
      const norm = normalizeUrl(inv.url);
      if (this._conns.has(norm)) continue;
      const conn = new ServerConnection(norm, {
        token: token || inv.token, kind, fetchImpl: this._fetch, WebSocketImpl: this._WS,
      });
      conn._onStatus = () => this._onChange({ type: 'status', connection: conn });
      try {
        await conn.handshake();
      } catch (err) {
        // A refused credential is remembered, not retried; any other failure may recover.
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

  // Rebuilt by re-inserting in order (unknown urls skipped, missing ones keep relative order
  // at the end); routes through _onChange so the new order persists like every mutation.
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

  // `token` overrides the stored credential (empty string = mint a fresh session).
  async reconnectOne(url, token) {
    const norm = normalizeUrl(url);
    const conn = this._conns.get(norm) || this._expired.get(norm);
    const reuse = token === undefined;
    const cred = reuse ? (conn ? conn.credential : '') : token;
    // A pasted token is of unknown kind and probes first; a reused one keeps its known kind.
    const kind = reuse ? (conn?.credentialKind || '') : '';
    if (conn) {
      conn.close();
      this._conns.delete(norm);
      this._expired.delete(norm);
    }
    // Keep the URL known for the whole handshake (_reconnecting).
    this._reconnecting.set(norm, {
      url: norm, status: 'connecting', connected: false,
      credentialKind: kind || conn?.credentialKind || '',
    });
    try {
      await this.connect(cred ? { url: norm, token: cred, kind } : norm);
    } finally {
      this._reconnecting.delete(norm);
    }
    return this;
  }

  async reconnect() {
    const set = this._lastSet.slice();
    this.disconnectAll();
    if (set.length) await this.connect(set);
    return this;
  }

  async remoteProjects() {
    const out = [];
    await Promise.all(this.connections.map(async (c) => {
      try { out.push(...await c.listProjects()); } catch { /* skip unreachable server */ }
    }));
    return out;
  }
}
