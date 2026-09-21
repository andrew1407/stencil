// Remote project sync helpers (browser ↔ collaboration server): create-on-server after a
// local create, and version-guarded save-back on save. Each takes a resolved
// ServerConnection; a 409 rethrows flagged `conflict`.
import type { ConnectionManager } from './connectionManager.js';
import type { ServerConnection, ProjectEventMessage } from './serverConnection.js';
import type { RemoteLink } from '../core/remote/remoteSyncController.js';
import type { ProjectLayout } from '../core/project/store/projectsStore.js';

export declare const CONFLICT_MESSAGE: string;

/** Should a server `project-event` reload the active editor? Pure; shared with the desktop poll. */
export declare const shouldReloadFromEvent: (
  msg: ProjectEventMessage | null | undefined,
  link: RemoteLink | null | undefined,
  opts?: { now?: number; lastLocalSaveAt?: number; isDrawing?: boolean; connUrl?: string | null; echoWindowMs?: number },
) => boolean;

/** Falls back to a direct authenticated DELETE with the saved token when the live connection is gone. */
export declare const deleteRemoteProject: (connMgr: ConnectionManager | null | undefined, serverUrl: string, id: string) => Promise<void>;

/** Throws a clear error when there is no live connection to `address`. */
export declare const requireConnection: (connMgr: ConnectionManager | null | undefined, address: string) => ServerConnection;

export interface RemoteImageBytes { bytes?: Uint8Array | null; ext?: string; w?: number; h?: number; }

export declare const createRemoteProject: (
  conn: ServerConnection,
  opts?: { name?: string; source?: string; resource?: string; color?: string } & RemoteImageBytes,
) => Promise<RemoteLink>;

export declare const saveRemoteProject: (
  conn: ServerConnection,
  link: RemoteLink,
  opts?: { name?: string; layout?: ProjectLayout } & RemoteImageBytes,
) => Promise<RemoteLink>;
