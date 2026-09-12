// ── Server connections: pin routing + the persisted high-level API ──────────
// The single import point every surface uses; the model, store and REST client behind
// it are re-exported so a caller never has to know which of the four it came from.
import { normalizeUrl, sharedPinsFromProjects } from './connectionModel.js';
import {
  dropConnection, isAdminConnection, loadConnections, saveConnections, upsertConnection,
} from './connectionStore.js';
import { connect, fetchImpl, listProjects } from './connectionRest.js';

export {
  CONNECTIONS_KEY, isLoopbackHost, mergePins, normalizeUrl, parseInviteUrl,
  sharedPinFromProject, sharedPinsFromProjects,
} from './connectionModel.js';
export {
  dropConnection, filterConnections, isAdminConnection, loadConnections, upsertConnection,
} from './connectionStore.js';
export { connect, createProject, fetchProjectImage, listProjects } from './connectionRest.js';

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
    // Only the minted SESSION token is persisted, so re-validating it can never re-prove
    // the admin credential behind it — carry the known kind over instead of losing it.
    if (isAdminConnection(existing)) conn.credentialKind = 'admin';
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
