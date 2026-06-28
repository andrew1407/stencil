// ── Server connections (extension) ──────────────────────────────────────────
// Connected servers (server/internal/protocol over REST) expose their projects as SHARED
// pins, persisted in chrome.storage.local; fetch/storage are injectable for `node --test`.

export const CONNECTIONS_KEY = 'stencil-connections';

// Normalize 'host:8090' / 'http://host:8090/' to a clean origin.
export const normalizeUrl = (raw) => {
  let s = String(raw == null ? '' : raw).trim();
  if (!s) throw new Error('Server URL is required');
  if (!/^https?:\/\//i.test(s)) s = 'http://' + s;
  return new URL(s).origin;
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

// ── connection persistence (chrome.storage.local) ──

const storage = () => globalThis.chrome?.storage?.local;

export const loadConnections = async () => {
  try {
    const o = await storage().get(CONNECTIONS_KEY);
    return Array.isArray(o[CONNECTIONS_KEY]) ? o[CONNECTIONS_KEY] : [];
  } catch {
    return [];
  }
};

const saveConnections = async (list) => {
  try { await storage().set({ [CONNECTIONS_KEY]: list }); } catch { /* storage unavailable */ }
};

// Pure: add/replace a connection record keyed by url (newest-first).
export const upsertConnection = (list, conn) => {
  const out = (Array.isArray(list) ? list : []).filter((c) => c.url !== conn.url);
  out.unshift({ url: conn.url, token: conn.token || '' });
  return out;
};

export const dropConnection = (list, url) =>
  (Array.isArray(list) ? list : []).filter((c) => c.url !== url);

// ── REST ──

const fetchImpl = () => globalThis.fetch?.bind(globalThis);

const req = async (conn, method, path, { body, raw, query, fetch: f = fetchImpl() } = {}) => {
  let url = conn.url + path;
  if (query) url += '?' + new URLSearchParams(query).toString();
  const headers = { Authorization: 'Bearer ' + conn.token };
  let payload = body;
  if (body != null && !raw) { headers['Content-Type'] = 'application/json'; payload = JSON.stringify(body); }
  const resp = await f(url, { method, headers, body: payload });
  if (!resp.ok) {
    let msg = `HTTP ${resp.status}`;
    try { const e = await resp.json(); if (e && e.message) msg = e.message; } catch { /* non-JSON */ }
    throw new Error(`${method} ${path}: ${msg}`);
  }
  if (resp.status === 204) return null;
  if (raw) return resp;
  return resp.json();
};

// Connect: normalize, then validate a supplied token or issue a fresh one.
export const connect = async (rawUrl, token = '', f = fetchImpl()) => {
  const url = normalizeUrl(rawUrl);
  let tok = token;
  if (!tok) {
    const r = await req({ url, token: '' }, 'POST', '/auth/token', { body: {}, fetch: f });
    tok = r.token;
  } else {
    await req({ url, token: tok }, 'GET', '/projects', { fetch: f });
  }
  return { url, token: tok };
};

// List a connection's projects.
export const listProjects = async (conn, f = fetchImpl()) => {
  const r = await req(conn, 'GET', '/projects', { fetch: f });
  return r.projects || [];
};

// Create a new project on a connection (used when pinning/adding to a server).
export const createProject = async (conn, { name, source = '', resource = '' }, f = fetchImpl()) =>
  req(conn, 'POST', '/projects', { body: { name, source, resource, hasImage: true }, fetch: f });

// Fetch a shared project's bytes as a Blob (Bearer-authed, so callers data-URL them).
// kind: 'original' (unedited, default — editor re-opens it to re-apply filter/lines) | 'result' (baked preview).
export const fetchProjectImage = async (conn, projectId, kind = 'original', f = fetchImpl()) => {
  const resp = await req(conn, 'GET', `/projects/${projectId}/files/${kind}`, { raw: true, fetch: f });
  return resp.blob();
};

// ── pin-target selection (pure) ──
// Route an incoming pin by connection count: 'none' (local only), 'one' (offer a
// "store on server" checkbox), or 'many' (offer a server picker).
export const pinTargetMode = (connections) => {
  const n = Array.isArray(connections) ? connections.length : 0;
  if (n === 0) return 'none';
  if (n === 1) return 'one';
  return 'many';
};

// Pure: find a connection by its url (the picker's selected value), or null.
export const connectionByUrl = (connections, url) =>
  (Array.isArray(connections) ? connections : []).find((c) => c && c.url === url) || null;

// Pure: map a scanned image / shared-pin row to a createProject body. `source`
// is the image's own URL and `resource` the page it came from (provenance).
export const projectRequestFromImage = (image = {}, resource = '') => ({
  name: image.name || 'Untitled',
  source: image.source || image.src || '',
  resource: image.resource || resource || '',
});

// Gather shared pins across every connected server (best-effort per server).
export const collectSharedPins = async (connections, f = fetchImpl()) => {
  const out = [];
  await Promise.all((connections || []).map(async (conn) => {
    try {
      const projects = await listProjects(conn, f);
      out.push(...sharedPinsFromProjects(projects, conn.url));
    } catch { /* unreachable server → skip */ }
  }));
  return out;
};

// ── high-level async API (persisted) ──

// Connect and persist; returns the updated connection list.
export const addServer = async (rawUrl, token = '', f = fetchImpl()) => {
  const conn = await connect(rawUrl, token, f);
  const next = upsertConnection(await loadConnections(), conn);
  await saveConnections(next);
  return next;
};

// Re-establish a persisted connection: re-validate its token, or issue a fresh one if
// that's rejected. Persists any new token. Throws if the server is unreachable.
export const reconnectServer = async (rawUrl, f = fetchImpl()) => {
  const url = normalizeUrl(rawUrl);
  const existing = (await loadConnections()).find((c) => c.url === url);
  let conn;
  try {
    conn = await connect(url, existing ? existing.token : '', f);  // re-validate token
  } catch {
    conn = await connect(url, '', f);  // token stale/rejected → request a fresh one
  }
  const next = upsertConnection(await loadConnections(), conn);
  await saveConnections(next);
  return next;
};

// Remove a persisted connection; returns the updated list.
export const removeServer = async (rawUrl) => {
  const url = normalizeUrl(rawUrl);
  const next = dropConnection(await loadConnections(), url);
  await saveConnections(next);
  return next;
};
