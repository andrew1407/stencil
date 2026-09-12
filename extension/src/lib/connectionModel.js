// ── Server connections (extension) ──────────────────────────────────────────
// Connected servers (server/internal/protocol over REST) expose their projects as SHARED
// pins, persisted in chrome.storage.local; fetch/storage are injectable for `node --test`.

export const CONNECTIONS_KEY = 'stencil-connections';

// True for a loopback host (localhost, *.localhost, 127.0.0.0/8, ::1), where plaintext
// http is safe because the bytes never leave the machine. Port of the browser's
// connectionManager.js isLoopbackHost.
export const isLoopbackHost = (host) => {
  if (!host) return false;
  const h = host.toLowerCase().replace(/^\[|\]$/g, ''); // strip any IPv6 brackets
  if (h === 'localhost' || h.endsWith('.localhost')) return true;
  if (h === '::1') return true;
  return /^127\.\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(h);
};

// Normalize 'host:8090' / 'http://host:8090/' to a clean origin. Secure by default
// (matches the browser's normalizeUrl): a bare REMOTE host gets https; loopback keeps
// http (dev servers run plaintext on localhost). An explicit scheme is preserved — the
// user opts into cleartext.
export const normalizeUrl = (raw) => {
  let s = String(raw == null ? '' : raw).trim();
  if (!s) throw new Error('Server URL is required');
  if (!/^https?:\/\//i.test(s)) {
    const host = new URL('http://' + s).hostname;
    s = (isLoopbackHost(host) ? 'http://' : 'https://') + s;
  }
  return new URL(s).origin;
};

// An invite link is a server URL carrying a session token in its FRAGMENT:
// `<url>#token=<value>` (the fragment never goes over the wire). Split it before
// normalizeUrl; any other fragment passes through untouched (origin drops it anyway).
// Port of the browser's connectionManager.js parseInviteUrl.
export const parseInviteUrl = (raw) => {
  const s = String(raw == null ? '' : raw);
  const at = s.indexOf('#');
  const m = at < 0 ? null : /^token=(.+)$/.exec(s.slice(at + 1));
  if (!m) return { url: s, token: '' };
  let token = m[1];
  try { token = decodeURIComponent(token); } catch { /* keep raw */ }
  return { url: s.slice(0, at), token };
};

// Map a server project to a shared-pin record, keyed by server origin + image source;
// `shared`/`serverUrl`/`projectId` drive the golden outline and route opens.
export const sharedPinFromProject = (proj, serverUrl) => ({
  source: `${serverUrl}/projects/${proj.id}/files/original`,
  // The project's ORIGINAL web source URL (what was pinned), so a local pin of the
  // same image can be matched to its server copy and shown with the golden outline.
  origin: proj.source || '',
  site: serverUrl,
  resource: proj.resource || '',
  name: proj.name || 'Untitled',
  // Project's custom accent colour ("#rrggbb", or "" = default) for the popup's pin-row name.
  color: proj.color || '',
  kind: 'image',
  t: proj.updatedAt || 0,
  shared: true,
  serverUrl,
  projectId: proj.id,
});

// Map a project list to shared pins, keeping only those with an image.
export const sharedPinsFromProjects = (projects, serverUrl) =>
  (Array.isArray(projects) ? projects : [])
    .filter((p) => p && p.hasImage)
    .map((p) => sharedPinFromProject(p, serverUrl));

// Merge local pins with shared pins (de-duped by serverUrl+projectId), newest-first
// within each group, shared listed after local.
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

