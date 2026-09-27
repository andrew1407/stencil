// The single import point for server connections; model, store and REST client re-exported.
import { normalizeUrl, sharedPinsFromProjects } from './model.js';
import {
  dropConnection, isAdminConnection, loadConnections, saveConnections, upsertConnection,
} from './store.js';
import { connect, fetchImpl, listProjectsIfChanged } from './rest.js';

export {
  CONNECTIONS_KEY, isLoopbackHost, mergePins, normalizeUrl, parseInviteUrl,
  sharedPinFromProject, sharedPinsFromProjects,
} from './model.js';
export {
  dropConnection, filterConnections, isAdminConnection, loadConnections, upsertConnection,
} from './store.js';
export {
  checkSession, connect, createProject, fetchProjectImage, listProjects, listProjectsIfChanged, SHARED_LIST_LIMIT,
} from './rest.js';

// 'none' (local only) | 'one' (a "store on server" checkbox) | 'many' (a server picker).
export const pinTargetMode = (connections) => {
  const n = Array.isArray(connections) ? connections.length : 0;
  if (n === 0) return 'none';
  if (n === 1) return 'one';
  return 'many';
};

export const connectionByUrl = (connections, url) =>
  (Array.isArray(connections) ? connections : []).find((c) => c && c.url === url) || null;

// `source` is the image's own URL and `resource` the page it came from.
export const projectRequestFromImage = (image = {}, resource = '') => ({
  name: image.name || 'Untitled',
  source: image.source || image.src || '',
  resource: image.resource || resource || '',
});

// Best-effort per server, each list riding its last pages' ETags in `cache` (url → { etag, pages, projects });
// `changed` is false only when every server answered what it answered last time.
export const refreshSharedPins = async (connections, cache = new Map(), f = fetchImpl()) => {
  const out = [];
  const listed = new Set();
  let changed = false;
  await Promise.all((connections || []).map(async (conn) => {
    listed.add(conn.url);
    const prev = cache.get(conn.url);
    let next = { etag: '', pages: [], projects: null };   // unreachable server → skipped
    try {
      const r = await listProjectsIfChanged(conn, prev && prev.projects ? prev : null, f);
      next = r.changed ? { etag: r.etag, pages: r.pages, projects: r.projects } : (prev || next);
    } catch { /* keep the empty answer */ }
    if (!prev || JSON.stringify(prev.projects) !== JSON.stringify(next.projects)) changed = true;
    cache.set(conn.url, next);
    if (next.projects) out.push(...sharedPinsFromProjects(next.projects, conn.url));
  }));
  for (const url of [...cache.keys()]) if (!listed.has(url)) { cache.delete(url); changed = true; }
  return { pins: out, changed };
};

export const collectSharedPins = async (connections, f = fetchImpl()) =>
  (await refreshSharedPins(connections, new Map(), f)).pins;

export const addServer = async (rawUrl, token = '', f = fetchImpl()) => {
  const conn = await connect(rawUrl, token, f);
  const next = upsertConnection(await loadConnections(), conn);
  await saveConnections(next);
  return next;
};

// Re-validates the stored token, or issues a fresh one if that's rejected.
export const reconnectServer = async (rawUrl, f = fetchImpl()) => {
  const url = normalizeUrl(rawUrl);
  const existing = (await loadConnections()).find((c) => c.url === url);
  let conn;
  try {
    conn = await connect(url, existing ? existing.token : '', f);
    // Only the SESSION token is persisted, so re-validating it can never re-prove the
    // admin credential behind it — carry the known kind over.
    if (isAdminConnection(existing)) conn.credentialKind = 'admin';
  } catch {
    conn = await connect(url, '', f);
  }
  const next = upsertConnection(await loadConnections(), conn);
  await saveConnections(next);
  return next;
};

export const removeServer = async (rawUrl) => {
  const url = normalizeUrl(rawUrl);
  const next = dropConnection(await loadConnections(), url);
  await saveConnections(next);
  return next;
};
