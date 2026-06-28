// ── Persisted server connections + auto-connect preference ──────────
// Remembers the connected server set ({url, token}) and an "auto-connect on
// open" toggle in localStorage, so the editor can re-establish servers across
// reloads. All localStorage access is guarded so importing this leaf in Node
// (the test runner) stays inert.

const SERVERS_KEY = 'drawingApp_servers';
const AUTOCONNECT_KEY = 'drawingApp_autoConnectServers';
const SYNC_KEY = 'drawingApp_syncToServer';

const ls = () => (typeof localStorage !== 'undefined' ? localStorage : null);

// The saved set: [{ url, token }]. Bad/missing data degrades to an empty list.
export const loadSavedServers = () => {
  try {
    const raw = ls()?.getItem(SERVERS_KEY);
    const arr = raw ? JSON.parse(raw) : [];
    return Array.isArray(arr) ? arr.filter((s) => s && s.url) : [];
  } catch {
    return [];
  }
};

export const saveServers = (list) => {
  try {
    const slim = (list || []).map((s) => ({ url: s.url, token: s.token || '' }));
    ls()?.setItem(SERVERS_KEY, JSON.stringify(slim));
  } catch {
    /* storage blocked — connections still work for this session, just won't persist */
  }
};

// Auto-connect defaults ON (restore servers on open); an explicit '0' disables it
// so the editor starts with every server closed until the user reconnects.
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

// "Sync changes to server" defaults ON: edits to a fetched server project push live to
// peers. An explicit '0' makes a fetched project edit-in-memory only — never pushed AND
// never auto-saved locally; the user can download it or "Make local copy" to persist.
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
