// ── The set of servers one editor session is connected to ───────────────────
// Connect/disconnect/reconnect over ServerConnection, plus the expired-credential set the
// Servers UI shows as rows offering Reconnect. URL + status rules live in ./urlRules.js.
import { normalizeUrl, parseInviteUrl } from './urlRules.js';
import { ServerConnection } from './serverConnection.js';

export {
  REMOTE_FLAG, isLoopbackHost, normalizeUrl, parseInviteUrl, buildInviteUrl,
  isInsecureRemote, wsUrl, isAuthStatus, isExpiredSession,
} from './urlRules.js';
export { ServerConnection } from './serverConnection.js';

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
    // A connection being RE-ESTABLISHED is still a known one: connect() only lands the new
    // ServerConnection once its handshake is through, and the status events in between
    // re-render the list, so without this the row (and the selection bar with it) blinked
    // out and back on every reconnect. The stand-in carries just what the list renders.
    this._reconnecting = new Map();   // url -> { url, status, connected, credentialKind }
    this._lastSet = [];        // for reconnect()
  }

  get urls() { return Array.from(this._conns.keys()); }
  get expiredUrls() { return Array.from(this._expired.keys()); }
  // Everything the user has a row for: live first, then the sessions that need a token.
  get knownUrls() {
    const known = [...this._conns.keys(), ...this._expired.keys()];
    if (!this._reconnecting.size) return known;   // the usual case: no de-duplication
    return [...new Set([...known, ...this._reconnecting.keys()])];
  }
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
    // The real connection first: connect() sets it while a reconnect's stand-in is still
    // there, and the settled row must win over the one that says "connecting".
    return this._conns.get(norm) || this._expired.get(norm)
        || this._reconnecting.get(norm) || null;
  }
  get last() { const u = this.urls; return u.length ? this._conns.get(u[u.length - 1]) : null; }

  // Connect one or more servers. Accepts a URL string, {url, token}, or an array
  // of either. Resolves once all are connected (already-connected urls are no-ops).
  async connect(spec) {
    const items = Array.isArray(spec) ? spec : [spec];
    for (const item of items) {
      const { url, token, kind } = typeof item === 'string' ? { url: item, token: '' } : (item || {});
      // Invite link: adopt the `#token=` fragment as the credential — an explicitly
      // supplied token always wins over the fragment one.
      const inv = parseInviteUrl(url);
      const norm = normalizeUrl(inv.url);
      if (this._conns.has(norm)) continue;
      const conn = new ServerConnection(norm, {
        token: token || inv.token, kind, fetchImpl: this._fetch, WebSocketImpl: this._WS,
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
    // Keep the URL known for the whole handshake (see _reconnecting) — the list must not
    // blink out from under the button that asked for this.
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
