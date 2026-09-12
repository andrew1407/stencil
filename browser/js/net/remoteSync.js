import { normalizeUrl } from './connectionManager.js';
import { loadSavedServers } from './connectionStore.js';
// Create-on-server after a local create + version-guarded save-back; each takes a
// resolved ServerConnection.

// Shown when a save-back loses the last-writer-wins race (HTTP 409).
export const CONFLICT_MESSAGE =
  'This project was edited elsewhere — reload it from the server before saving again.';

// Should a server `project-event` reload the active editor? Only an "updated" event for
// THIS linked project with a newer version, not mid-stroke, outside our own save's echo
// window. Shared by the browser feed and the desktop poll.
export const shouldReloadFromEvent = (msg, link, opts = {}) => {
  // version<=link.version is the real self-echo guard; echoWindowMs covers the brief race,
  // kept short so a peer's change right after our save is not mistaken for our echo.
  const { now = Date.now(), lastLocalSaveAt = 0, isDrawing = false, connUrl = null,
          echoWindowMs = 150 } = opts;
  if (!link || !msg || msg.type !== 'project-event' || msg.event !== 'updated') return false;
  const proj = msg.project;
  if (!proj || proj.id !== link.remoteId) return false;
  if (connUrl != null && connUrl !== link.address) return false;
  if (isDrawing) return false;
  const v = proj.version;
  if (typeof v !== 'number' || v <= (link.version || 0)) return false;
  if (now - lastLocalSaveAt < echoWindowMs) return false;
  return true;
};

// Falls back to a direct authenticated DELETE with the saved token when this tab's live
// connection object is gone (dropped feed, cached listing).
export const deleteRemoteProject = async (connMgr, serverUrl, id) => {
  const conn = connMgr?.get(serverUrl);
  if (conn) { await conn.deleteProject(id); return; }
  const saved = loadSavedServers().find((s) => {
    try { return normalizeUrl(s.url) === normalizeUrl(serverUrl); } catch { return false; }
  });
  if (!saved) throw new Error(`not connected to ${serverUrl}`);
  const res = await fetch(`${normalizeUrl(serverUrl)}/projects/${encodeURIComponent(id)}`, {
    method: 'DELETE', headers: { Authorization: `Bearer ${saved.token}` },
  });
  if (!res.ok && res.status !== 404) {   // already-gone counts as removed
    let msg = `HTTP ${res.status}`;
    try { const body = await res.json(); if (body && body.message) msg = body.message; } catch { /* not JSON */ }
    throw new Error(msg);
  }
};

// Throws when there is no live connection to that server.
export const requireConnection = (connMgr, address) => {
  if (!connMgr) throw new Error('No server connections — connect a server first');
  const conn = connMgr.get(address);
  if (!conn) throw new Error(`Not connected to server "${address}" — connect it first`);
  return conn;
};

// File writes bump the version but their response carries none, so re-read it to keep
// the link's guard accurate for the next save.
const currentVersion = async (conn, id, fallback) => {
  try {
    const full = await conn.getProject(id);
    const v = full && full.project ? full.project.version : undefined;
    return v == null ? fallback : v;
  } catch {
    return fallback;
  }
};

// Returns the session link { address, remoteId, version } to persist on the editor.
export const createRemoteProject = async (conn, { name, source, resource, color, bytes, ext, w, h } = {}) => {
  const rec = await conn.createProject({
    name: name || 'Untitled',
    source: source || '',
    resource: resource || '',
    color: color || '',
    hasImage: !!bytes,
  });
  let version = rec && rec.version != null ? rec.version : 0;
  if (bytes && bytes.length) {
    await conn.putFile(rec.id, 'original', bytes, { ext: ext || 'png', w: w || 0, h: h || 0 });
    version = await currentVersion(conn, rec.id, version);
  }
  return { address: conn.url, remoteId: rec.id, version };
};

// A 409 (lost LWW race) is rethrown with err.conflict === true.
export const saveRemoteProject = async (conn, link, { name, layout, bytes, ext, w, h } = {}) => {
  let rec;
  try {
    rec = await conn.updateProject(link.remoteId, { name, layout, version: link.version });
  } catch (err) {
    if (err && err.status === 409) {
      const e = new Error(CONFLICT_MESSAGE);
      e.conflict = true;
      throw e;
    }
    throw err;
  }
  let version = rec && rec.version != null ? rec.version : link.version;
  if (bytes && bytes.length) {
    await conn.putFile(link.remoteId, 'result', bytes, { ext: ext || 'png', w: w || 0, h: h || 0 });
    version = await currentVersion(conn, link.remoteId, version);
  }
  return { ...link, version };
};
