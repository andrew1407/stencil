// One connected Stencil server: a token, a live /ws events feed and the REST surface
// (server/internal/protocol). fetch + WebSocket are injected so `node --test` can drive
// it without either. A refused credential lands in the 'expired' status, not 'error'.
import type { ProjectLayout } from '../core/projectsStore.js';

export type ConnectionStatus = 'connecting' | 'connected' | 'error' | 'expired' | 'disconnected';

/** A project record as the server returns it (protocol Project). */
export interface RemoteProjectRecord {
  id: string;
  name: string;
  source?: string;
  resource?: string;
  color?: string;
  version: number;
  hasImage?: boolean;
  expiresAt?: number;
  keywords?: string[];
  description?: string;
  updatedAt?: number;
  [field: string]: unknown;
}

/** A listing row after tagRemote: flagged remote and stamped with its server. */
export interface TaggedRemoteProject extends RemoteProjectRecord { remote: true; serverUrl: string; }

export interface RemoteProjectFull { project: RemoteProjectRecord; layout?: ProjectLayout | null; }

/** A live `project-event` from the /ws feed. */
export interface ProjectEventMessage {
  type: 'project-event';
  event: 'created' | 'updated' | 'deleted';
  project?: RemoteProjectRecord;
}

/** A REST failure; `expired` marks a credential the server will never accept again. */
export interface ServerRequestError extends Error { status?: number; expired?: boolean; }

export type FileKind = 'original' | 'result' | 'video' | 'chat' | string;

export interface ServerConnectionOptions {
  token?: string;
  kind?: 'admin' | '';
  fetchImpl?: typeof fetch;
  WebSocketImpl?: typeof WebSocket;
  clientId?: string;
}

export declare class ServerConnection {
  constructor(url: string, opts?: ServerConnectionOptions);
  readonly url: string;
  /** The session token in use; minted from `credential` when that is an admin token. */
  token: string;
  /** What the user supplied — persisted, since a minted session dies with the server. */
  credential: string;
  credentialKind: 'admin' | '';
  clientId: string;
  connected: boolean;
  status: ConnectionStatus;
  /** Acquire/validate a token, then verify access by listing projects. Rejects with `expired` set on a refusal. */
  handshake(): Promise<this>;
  listProjects(): Promise<TaggedRemoteProject[]>;
  getProject(id: string): Promise<RemoteProjectFull>;
  createProject(body: Partial<RemoteProjectRecord>): Promise<TaggedRemoteProject>;
  updateProject(id: string, body: { name?: string; layout?: ProjectLayout; version?: number } & Record<string, unknown>): Promise<TaggedRemoteProject>;
  deleteProject(id: string): Promise<null>;
  /** The server is codec-free, so dimensions ride as query params. */
  putFile(id: string, kind: FileKind, bytes: Uint8Array, opts?: { ext?: string; w?: number; h?: number }): Promise<unknown>;
  fileUrl(id: string, kind: FileKind): string;
  deleteFile(id: string, kind: FileKind): Promise<null>;
  fetchFile(id: string, kind: FileKind): Promise<Blob>;
  /** A fresh session token wrapped as `<url>#token=<token>`. */
  mintInvite(): Promise<string>;
  tagRemote<T extends object>(p: T): T & { remote: true; serverUrl: string };
  onEvent(cb: (msg: ProjectEventMessage, conn: ServerConnection) => void): () => void;
  close(): void;
}
