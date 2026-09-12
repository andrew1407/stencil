// ── Server URL rules, invite links and credential statuses ──────────────────
// The pure half of the connection layer: everything a URL or an HTTP status answers on
// its own, with no socket and no fetch behind it. Shared by ServerConnection, the
// manager, and the connect UI.

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

// An invite link is a server URL carrying a session token in its FRAGMENT:
// `<url>#token=<value>` (the fragment never goes over the wire). Split it before
// normalizeUrl; any other fragment passes through untouched (origin drops it anyway).
export const parseInviteUrl = (raw) => {
  const s = String(raw == null ? '' : raw);
  const at = s.indexOf('#');
  const m = at < 0 ? null : /^token=(.+)$/.exec(s.slice(at + 1));
  if (!m) return { url: s, token: '' };
  let token = m[1];
  try { token = decodeURIComponent(token); } catch { /* keep raw */ }
  return { url: s.slice(0, at), token };
};

// The inverse: build `<normalized-url>#token=<token>` for sharing.
export const buildInviteUrl = (url, token) =>
  `${normalizeUrl(url)}#token=${encodeURIComponent(token)}`;

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
