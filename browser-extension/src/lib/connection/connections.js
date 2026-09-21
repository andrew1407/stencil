// The single import point for server connections; model, store and REST client re-exported.
import { normalizeUrl, sharedPinsFromProjects } from './model.js';
import {
  dropConnection, isAdminConnection, loadConnections, saveConnections, upsertConnection,
} from './store.js';
import { connect, fetchImpl, listProjects } from './rest.js';

export {
  CONNECTIONS_KEY, isLoopbackHost, mergePins, normalizeUrl, parseInviteUrl,
  sharedPinFromProject, sharedPinsFromProjects,
} from './model.js';
export {
  dropConnection, filterConnections, isAdminConnection, loadConnections, upsertConnection,
} from './store.js';
export { connect, createProject, fetchProjectImage, listProjects } from './rest.js';

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

// Best-effort per server.
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
