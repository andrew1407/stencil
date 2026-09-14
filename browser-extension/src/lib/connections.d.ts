// The single import point for server connections — re-exports connectionModel.js,
// connectionStore.js and connectionRest.js, plus its own combinators.
export declare const CONNECTIONS_KEY: string;
export declare const isLoopbackHost: (host: string) => boolean;
export declare const normalizeUrl: (raw: string) => string;
export declare const parseInviteUrl: (raw: string) => { url: string; token: string };

export interface ServerProject {
  id: string; name?: string; source?: string; resource?: string; color?: string;
  updatedAt?: number; hasImage?: boolean;
}
export interface SharedPin {
  source: string; origin: string; site: string; resource: string; name: string; color: string;
  kind: 'image'; t: number; shared: true; serverUrl: string; projectId: string;
}
export declare const sharedPinFromProject: (proj: ServerProject, serverUrl: string) => SharedPin;
export declare const sharedPinsFromProjects: (projects: ServerProject[], serverUrl: string) => SharedPin[];

export interface Connection { url: string; token: string; credential: string; credentialKind: '' | 'admin'; }
export interface StoredConnection { url: string; token: string; credentialKind: '' | 'admin'; }
export declare const dropConnection: (list: StoredConnection[], url: string) => StoredConnection[];
export declare const filterConnections: (list: StoredConnection[],
  mode?: 'all' | 'admin' | 'other') => StoredConnection[];
export declare const isAdminConnection: (conn: StoredConnection | null | undefined) => boolean;
export declare const loadConnections: () => Promise<StoredConnection[]>;
export declare const upsertConnection: (list: StoredConnection[], conn: Connection) => StoredConnection[];

export type Fetch = typeof fetch;
export declare const connect: (rawUrl: string, token?: string, f?: Fetch) => Promise<Connection>;
export declare const createProject: (conn: Connection, project: { name: string; source?: string;
  resource?: string }, f?: Fetch) => Promise<ServerProject>;
export declare const fetchProjectImage: (conn: Connection, projectId: string,
  kind?: 'original' | 'result', f?: Fetch) => Promise<Blob>;
export declare const listProjects: (conn: Connection, f?: Fetch) => Promise<ServerProject[]>;

export declare const pinTargetMode: (connections: StoredConnection[]) => 'none' | 'one' | 'many';
export declare const connectionByUrl: (connections: StoredConnection[], url: string) => StoredConnection | null;
export interface ProjectRequest { name: string; source: string; resource: string; }
export declare const projectRequestFromImage: (image?: Record<string, unknown>, resource?: string) => ProjectRequest;
export declare const collectSharedPins: (connections: StoredConnection[], f?: Fetch) => Promise<SharedPin[]>;
export declare const addServer: (rawUrl: string, token?: string, f?: Fetch) => Promise<StoredConnection[]>;
export declare const reconnectServer: (rawUrl: string, f?: Fetch) => Promise<StoredConnection[]>;
export declare const removeServer: (rawUrl: string) => Promise<StoredConnection[]>;
