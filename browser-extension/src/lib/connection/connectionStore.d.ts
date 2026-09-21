export interface StoredConnection { url: string; token: string; credentialKind: '' | 'admin'; }
export interface ConnectLike { url: string; token?: string; credentialKind?: string; }

export declare const loadConnections: () => Promise<StoredConnection[]>;
export declare const saveConnections: (list: StoredConnection[]) => Promise<void>;
export declare const upsertConnection: (list: StoredConnection[], conn: ConnectLike) => StoredConnection[];
export declare const dropConnection: (list: StoredConnection[], url: string) => StoredConnection[];
export declare const isAdminConnection: (conn: ConnectLike | null | undefined) => boolean;
export declare const filterConnections: (list: StoredConnection[],
  mode?: 'all' | 'admin' | 'other') => StoredConnection[];
