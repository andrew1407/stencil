// Persisted server connections + the auto-connect and sync-to-server preferences, in
// localStorage. Every access is guarded so importing this leaf in Node stays inert.

/** One saved server: the ORIGINAL credential, its kind, and whether the server refused it. */
export interface SavedServer {
  url: string;
  token: string;
  /** An admin token mints a session; a session token is used as-is. */
  kind?: 'admin';
  /** A refused credential is remembered so boot never spends a request on it. */
  expired?: boolean;
}

export declare const loadSavedServers: () => SavedServer[];
export declare const saveServers: (list: readonly SavedServer[] | null | undefined) => void;
/** Defaults ON: restore the saved server set on open. */
export declare const getAutoConnect: () => boolean;
export declare const setAutoConnect: (on: boolean) => void;
/** Defaults ON: edits to a fetched server project push live; OFF = edit-in-memory only. */
export declare const getSyncToServer: () => boolean;
export declare const setSyncToServer: (on: boolean) => void;
