// The saved connection list (chrome.storage.local) and the pure list operations over it.
import { CONNECTIONS_KEY } from './model.js';

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

// Keyed by url, newest-first; credentialKind lets the options list tell an ADMIN connection.
export const upsertConnection = (list, conn) => {
  const out = (Array.isArray(list) ? list : []).filter((c) => c.url !== conn.url);
  out.unshift({ url: conn.url, token: conn.token || '', credentialKind: conn.credentialKind === 'admin' ? 'admin' : '' });
  return out;
};

export const dropConnection = (list, url) =>
  (Array.isArray(list) ? list : []).filter((c) => c.url !== url);

// Rows saved before the field existed simply aren't admin.
export const isAdminConnection = (conn) => !!conn && conn.credentialKind === 'admin';

export const filterConnections = (list, mode = 'all') => {
  const arr = (Array.isArray(list) ? list : []).filter(Boolean);
  if (mode === 'admin') return arr.filter(isAdminConnection);
  if (mode === 'other') return arr.filter((c) => !isAdminConnection(c));
  return arr;
};
