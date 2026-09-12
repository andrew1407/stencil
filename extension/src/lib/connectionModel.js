// Connected servers (server/internal/protocol over REST) expose their projects as SHARED pins.

export const CONNECTIONS_KEY = 'stencil-connections';

// Loopback (localhost, *.localhost, 127.0.0.0/8, ::1) is where plaintext http is safe.
// Port of the browser's connectionManager.js isLoopbackHost.
export const isLoopbackHost = (host) => {
  if (!host) return false;
  const h = host.toLowerCase().replace(/^\[|\]$/g, '');
  if (h === 'localhost' || h.endsWith('.localhost')) return true;
  if (h === '::1') return true;
  return /^127\.\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(h);
};

// Secure by default: a bare REMOTE host gets https, loopback keeps http, and an explicit
// scheme is preserved (the user opts into cleartext). Matches the browser's normalizeUrl.
export const normalizeUrl = (raw) => {
  let s = String(raw == null ? '' : raw).trim();
  if (!s) throw new Error('Server URL is required');
  if (!/^https?:\/\//i.test(s)) {
    const host = new URL('http://' + s).hostname;
    s = (isLoopbackHost(host) ? 'http://' : 'https://') + s;
  }
  return new URL(s).origin;
};

// An invite link carries its token in the FRAGMENT (`<url>#token=<value>`), which never
// goes over the wire. Port of the browser's connectionManager.js parseInviteUrl.
export const parseInviteUrl = (raw) => {
  const s = String(raw == null ? '' : raw);
  const at = s.indexOf('#');
  const m = at < 0 ? null : /^token=(.+)$/.exec(s.slice(at + 1));
  if (!m) return { url: s, token: '' };
  let token = m[1];
  try { token = decodeURIComponent(token); } catch { /* keep raw */ }
  return { url: s.slice(0, at), token };
};

export const sharedPinFromProject = (proj, serverUrl) => ({
  source: `${serverUrl}/projects/${proj.id}/files/original`,
  // The ORIGINAL web source, so a local pin of the same image matches its server copy.
  origin: proj.source || '',
  site: serverUrl,
  resource: proj.resource || '',
  name: proj.name || 'Untitled',
  color: proj.color || '',
  kind: 'image',
  t: proj.updatedAt || 0,
  shared: true,
  serverUrl,
  projectId: proj.id,
});

export const sharedPinsFromProjects = (projects, serverUrl) =>
  (Array.isArray(projects) ? projects : [])
    .filter((p) => p && p.hasImage)
    .map((p) => sharedPinFromProject(p, serverUrl));

// De-duped by serverUrl+projectId; shared listed after local.
export const mergePins = (local, shared) => {
  const out = (Array.isArray(local) ? local : []).map((p) => ({ ...p, shared: false }));
  const seen = new Set();
  for (const s of (Array.isArray(shared) ? shared : [])) {
    const k = `${s.serverUrl}\n${s.projectId}`;
    if (seen.has(k)) continue;
    seen.add(k);
    out.push(s);
  }
  return out;
};

