export type Fetch = typeof fetch;
export declare const fetchImpl: () => Fetch | undefined;

export interface Connection { url: string; token: string; credential: string; credentialKind: '' | 'admin'; }
export declare const connect: (rawUrl: string, token?: string, f?: Fetch) => Promise<Connection>;
/** GET /auth/session; a server without it (404) is asked for one project instead. */
export declare const checkSession: (conn: { url: string; token: string }, f?: Fetch) => Promise<void>;

export interface ServerProject {
  id: string; name?: string; source?: string; resource?: string; hasImage?: boolean;
  color?: string; updatedAt?: number;
}
/** Pages a listing follows before it gives up rather than loop on a server's cursors. */
export declare const MAX_LIST_PAGES: number;
/** Every project, page by page through `nextCursor`; a repeated cursor throws. */
export declare const listProjects: (conn: Connection, f?: Fetch) => Promise<ServerProject[]>;
export declare const SHARED_LIST_LIMIT: number;
/** One page of a walk: the cursor that asked for it, its ETag, its rows and the cursor it names next. */
export interface ProjectListPage { after: string; etag: string; projects: ServerProject[]; next?: unknown; }
/** `changed: false` (every page a 304) carries no projects: the caller keeps the list it has. */
export interface ProjectListAnswer { changed: boolean; etag: string; pages: ProjectListPage[]; projects: ServerProject[] | null; }
export declare const listProjectsIfChanged: (conn: Connection, prev?: { pages?: ProjectListPage[] } | null,
  f?: Fetch) => Promise<ProjectListAnswer>;
export declare const createProject: (conn: Connection, project: { name: string; source?: string;
  resource?: string }, f?: Fetch) => Promise<ServerProject>;
export declare const fetchProjectImage: (conn: Connection, projectId: string,
  kind?: 'original' | 'result', f?: Fetch) => Promise<Blob>;
