// The connected server set ({url, token}) and the auto-connect / sync toggles in
// localStorage, guarded so importing this leaf in Node stays inert.

const SERVERS_KEY = 'drawingApp_servers';
const AUTOCONNECT_KEY = 'drawingApp_autoConnectServers';
const SYNC_KEY = 'drawingApp_syncToServer';

const ls = () => (typeof localStorage !== 'undefined' ? localStorage : null);

// The saved set: [{ url, token }]. Bad/missing data degrades to an empty list.
export const loadSavedServers = () => {
  try {
    const raw = ls()?.getItem(SERVERS_KEY);
    const arr = raw ? JSON.parse(raw) : [];
    // `expired` rides along: boot shows the row without spending a request on a dead token.
    return Array.isArray(arr) ? arr.filter((s) => s && s.url) : [];
  } catch {
    return [];
  }
};

export const saveServers = (list) => {
  try {
    const slim = (list || []).map((s) => {
      const out = { url: s.url, token: s.token || '' };
      // An admin token mints, a session token is used as-is; remembered so the next connect
      // never probes an admin token as a session one.
      if (s.kind === 'admin') out.kind = 'admin';
      if (s.expired) out.expired = true;
      return out;
    });
    ls()?.setItem(SERVERS_KEY, JSON.stringify(slim));
  } catch {
    /* storage blocked — connections still work for this session, just won't persist */
  }
};

// Defaults ON; an explicit '0' starts the editor with every server closed.
export const getAutoConnect = () => {
  try {
    return ls()?.getItem(AUTOCONNECT_KEY) !== '0';
  } catch {
    return true;
  }
};

export const setAutoConnect = (on) => {
  try {
    ls()?.setItem(AUTOCONNECT_KEY, on ? '1' : '0');
  } catch {
    /* ignore — preference just won't persist */
  }
};

// Defaults ON: edits to a fetched server project push live to peers. '0' makes it
// edit-in-memory only — never pushed AND never auto-saved locally.
export const getSyncToServer = () => {
  try {
    return ls()?.getItem(SYNC_KEY) !== '0';
  } catch {
    return true;
  }
};

export const setSyncToServer = (on) => {
  try {
    ls()?.setItem(SYNC_KEY, on ? '1' : '0');
  } catch {
    /* ignore — preference just won't persist */
  }
};
