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
  #fetch; #WS; #onChange;
  #conns = new Map();   // url -> ServerConnection
  // Refused credentials, kept out of the live set but remembered so the row keeps its URL
  // and offers a new token.
  #expired = new Map();
  // A connection being re-established stays known, or the row (and selection bar) would
  // blink out during the handshake; the stand-in carries just what the list renders.
  #reconnecting = new Map();   // url -> { url, status, connected, credentialKind }
  #lastSet = [];        // for reconnect()
  constructor({ fetchImpl, WebSocketImpl, onChange } = {}) {
    this.#fetch = fetchImpl; this.#WS = WebSocketImpl;
    this.#onChange = onChange || (() => {});
  }

  get urls() { return Array.from(this.#conns.keys()); }
  get expiredUrls() { return Array.from(this.#expired.keys()); }
  // Everything the user has a row for: live first, then the sessions that need a token.
  get knownUrls() {
    const known = [...this.#conns.keys(), ...this.#expired.keys()];
    if (!this.#reconnecting.size) return known;
    return [...new Set([...known, ...this.#reconnecting.keys()])];
  }
  isExpired(url) { try { return this.#expired.has(normalizeUrl(url)); } catch { return false; } }
  get connections() { return Array.from(this.#conns.values()); }
  get reconnectable() { return this.#conns.size > 0 || this.#expired.size > 0 || this.#lastSet.length > 0; }
  // The ORIGINAL credential persists — a minted session token dies with the server.
  // Expired sessions are included: the saved URL must survive a dead token.
  snapshot() {
    return [
      ...this.connections.map((c) => (c.credentialKind === 'admin'
        ? { url: c.url, token: c.credential || '', kind: 'admin' }
        : { url: c.url, token: c.credential || '' })),
      // The refusal persists too: the next boot must not spend a request (and a 401) on it.
      ...[...this.#expired.values()].map((c) => ({ url: c.url, token: c.credential || '', expired: true })),
    ];
  }

  // A saved entry already known to be refused goes straight to the expired set, no request.
  adoptExpired({ url, token = '', kind = '' } = {}) {
    const norm = normalizeUrl(url);
    if (this.#conns.has(norm) || this.#expired.has(norm)) return this;
    const conn = new ServerConnection(norm, { token, kind, fetchImpl: this.#fetch, WebSocketImpl: this.#WS });
    conn._onStatus = () => this.#onChange({ type: 'status', connection: conn });
    conn._setStatus('expired');
    this.#expired.set(norm, conn);
    this.#onChange({ type: 'expired', connection: conn });
    return this;
  }
  // has() answers "is there a USABLE connection here" — an expired one is not.
  has(url) { return this.#conns.has(normalizeUrl(url)); }
  // Expired sessions included, so the UI renders their status without a second lookup.
  get(url) {
    const norm = normalizeUrl(url);
    // The real connection first: the settled row must win over the reconnect stand-in.
    return this.#conns.get(norm) || this.#expired.get(norm)
        || this.#reconnecting.get(norm) || null;
  }
  get last() { const u = this.urls; return u.length ? this.#conns.get(u[u.length - 1]) : null; }

  // A URL string, {url, token}, or an array of either; already-connected urls are no-ops.
  async connect(spec) {
    const items = Array.isArray(spec) ? spec : [spec];
    for (const item of items) {
      const { url, token, kind } = typeof item === 'string' ? { url: item, token: '' } : (item || {});
      // An explicitly supplied token wins over the invite fragment's.
      const inv = parseInviteUrl(url);
      const norm = normalizeUrl(inv.url);
      if (this.#conns.has(norm)) continue;
      const conn = new ServerConnection(norm, {
        token: token || inv.token, kind, fetchImpl: this.#fetch, WebSocketImpl: this.#WS,
      });
      conn._onStatus = () => this.#onChange({ type: 'status', connection: conn });
      try {
        await conn.handshake();
      } catch (err) {
        // A refused credential is remembered, not retried; any other failure may recover.
        if (err.expired) {
          this.#expired.set(norm, conn);
          this.#onChange({ type: 'expired', connection: conn });
        }
        throw err;
      }
      this.#expired.delete(norm);
      conn.onEvent((msg, c) => this.#onChange({ type: 'event', message: msg, connection: c }));
      this.#conns.set(norm, conn);
    }
    this.#lastSet = this.snapshot();
    this.#onChange({ type: 'connect' });
    return this;
  }

  disconnect(url) {
    let target;
    if (url == null) { const k = this.knownUrls; target = k[k.length - 1]; }
    else target = normalizeUrl(url);
    const conn = target && (this.#conns.get(target) || this.#expired.get(target));
    if (conn) { conn.close(); this.#conns.delete(target); this.#expired.delete(target); }
    this.#onChange({ type: 'disconnect' });
    return this;
  }

  disconnectAll() {
    for (const c of this.#conns.values()) c.close();
    for (const c of this.#expired.values()) c.close();
    this.#conns.clear();
    this.#expired.clear();
    this.#onChange({ type: 'disconnect' });
    return this;
  }

  // Rebuilt by re-inserting in order (unknown urls skipped, missing ones keep relative order
  // at the end); routes through #onChange so the new order persists like every mutation.
  reorder(orderedUrls) {
    const next = new Map();
    for (const u of orderedUrls || []) {
      let norm; try { norm = normalizeUrl(u); } catch { continue; }
      if (this.#conns.has(norm) && !next.has(norm)) next.set(norm, this.#conns.get(norm));
    }
    for (const [k, v] of this.#conns) if (!next.has(k)) next.set(k, v);
    this.#conns = next;
    this.#lastSet = this.snapshot();
    this.#onChange({ type: 'reorder' });
    return this;
  }

  // `token` overrides the stored credential (empty string = mint a fresh session).
  async reconnectOne(url, token) {
    const norm = normalizeUrl(url);
    const conn = this.#conns.get(norm) || this.#expired.get(norm);
    const reuse = token === undefined;
    const cred = reuse ? (conn ? conn.credential : '') : token;
    // A pasted token is of unknown kind and probes first; a reused one keeps its known kind.
    const kind = reuse ? (conn?.credentialKind || '') : '';
    if (conn) {
      conn.close();
      this.#conns.delete(norm);
      this.#expired.delete(norm);
    }
    // Keep the URL known for the whole handshake (#reconnecting).
    this.#reconnecting.set(norm, {
      url: norm, status: 'connecting', connected: false,
      credentialKind: kind || conn?.credentialKind || '',
    });
    try {
      await this.connect(cred ? { url: norm, token: cred, kind } : norm);
    } finally {
      this.#reconnecting.delete(norm);
    }
    return this;
  }

  async reconnect() {
    const set = this.#lastSet.slice();
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
