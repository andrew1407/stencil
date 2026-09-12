// ── Connection persistence (chrome.storage.local) ───────────────────────────
// The saved connection list and the pure list operations over it. `credentialKind`
// rides along so the options list can tell an ADMIN connection from a session one.
import { CONNECTIONS_KEY } from './connectionModel.js';

const storage = () => globalThis.chrome?.storage?.local;

export const loadConnections = async () => {
  try {
    const o = await storage().get(CONNECTIONS_KEY);
    return Array.isArray(o[CONNECTIONS_KEY]) ? o[CONNECTIONS_KEY] : [];
  } catch {
    return [];
  }
};

export const saveConnections = async (list) => {
  try { await storage().set({ [CONNECTIONS_KEY]: list }); } catch { /* storage unavailable */ }
};

// Pure: add/replace a connection record keyed by url (newest-first). credentialKind
// rides along so the options list can tell an ADMIN connection from a session one.
export const upsertConnection = (list, conn) => {
  const out = (Array.isArray(list) ? list : []).filter((c) => c.url !== conn.url);
  out.unshift({ url: conn.url, token: conn.token || '', credentialKind: conn.credentialKind === 'admin' ? 'admin' : '' });
  return out;
};

export const dropConnection = (list, url) =>
  (Array.isArray(list) ? list : []).filter((c) => c.url !== url);

// True when the connection was established with an ADMIN credential — one that can mint
// session tokens. Rows saved before the field existed simply aren't admin.
export const isAdminConnection = (conn) => !!conn && conn.credentialKind === 'admin';

// View-only three-way filter for the options list: 'all' | 'admin' | 'other'.
export const filterConnections = (list, mode = 'all') => {
  const arr = (Array.isArray(list) ? list : []).filter(Boolean);
  if (mode === 'admin') return arr.filter(isAdminConnection);
  if (mode === 'other') return arr.filter((c) => !isAdminConnection(c));
  return arr;
};
