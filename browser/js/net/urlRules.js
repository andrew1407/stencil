// The pure half of the connection layer: what a URL or an HTTP status answers on its own.

// Remote project ids are namespaced so they never collide with local base36 ids; each
// remote meta carries `serverUrl` with `remote: true`.
export const REMOTE_FLAG = 'remote';

// Plaintext http is safe on loopback: the bytes never leave the machine.
export const isLoopbackHost = (host) => {
  if (!host) return false;
  const h = host.toLowerCase().replace(/^\[|\]$/g, ''); // strip any IPv6 brackets
  if (h === 'localhost' || h.endsWith('.localhost')) return true;
  if (h === '::1') return true;
  return /^127\.\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(h);
};

// 'host:8090' → a clean origin. A bare REMOTE host gets https, loopback keeps http; an
// explicit scheme is preserved — the user opts into cleartext.
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

// `<url>#token=<value>`: the fragment never goes over the wire. Split before normalizeUrl.
export const parseInviteUrl = (raw) => {
  const s = String(raw == null ? '' : raw);
  const at = s.indexOf('#');
  const m = at < 0 ? null : /^token=(.+)$/.exec(s.slice(at + 1));
  if (!m) return { url: s, token: '' };
  let token = m[1];
  try { token = decodeURIComponent(token); } catch { /* keep raw */ }
  return { url: s.slice(0, at), token };
};

export const buildInviteUrl = (url, token) =>
  `${normalizeUrl(url)}#token=${encodeURIComponent(token)}`;

// Cleartext bearer token + image bytes to a remote host — the UI warns on these.
export const isInsecureRemote = (origin) => {
  try {
    const u = new URL(origin);
    return u.protocol === 'http:' && !isLoopbackHost(u.hostname);
  } catch { return false; }
};

export const wsUrl = (origin) => origin.replace(/^http/i, 'ws') + '/ws';

// "Your credential was refused", not "the server is not there".
export const isAuthStatus = (status) => status === 401 || status === 403;
// The same question of a thrown REST error (the chat provider hits it on /llm/chat too).
export const isExpiredSession = (err) => !!err && (err.expired === true || isAuthStatus(err.status));
