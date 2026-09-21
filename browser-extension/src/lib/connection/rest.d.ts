export type Fetch = typeof fetch;
export declare const fetchImpl: () => Fetch | undefined;

export interface Connection { url: string; token: string; credential: string; credentialKind: '' | 'admin'; }
export declare const connect: (rawUrl: string, token?: string, f?: Fetch) => Promise<Connection>;

export interface ServerProject {
  id: string; name?: string; source?: string; resource?: string; hasImage?: boolean;
  color?: string; updatedAt?: number;
}
export declare const listProjects: (conn: Connection, f?: Fetch) => Promise<ServerProject[]>;
export declare const createProject: (conn: Connection, project: { name: string; source?: string;
  resource?: string }, f?: Fetch) => Promise<ServerProject>;
export declare const fetchProjectImage: (conn: Connection, projectId: string,
  kind?: 'original' | 'result', f?: Fetch) => Promise<Blob>;
