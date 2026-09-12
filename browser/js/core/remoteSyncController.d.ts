// Live co-edit push/pull + server writes: the debounced save-back, the peer-event reload,
// the conflict-merge retry loop and the server layout-adoption helpers. Holds only sync
// timing state; the session link, connections and the editor model live on the app.
import type { DrawingApp } from './drawingApp.js';
import type { ProjectLayout } from './projectsStore.js';
import type { ServerConnection, ProjectEventMessage } from '../net/serverConnection.js';

/** The editor's link to a server project; version is the save-back guard. */
export interface RemoteLink { address: string; remoteId: string; version: number; }

export declare class RemoteSyncController {
  constructor(app: DrawingApp);
  app: DrawingApp;
  scheduleRemoteSync(): void;
  onServerProjectEvent(msg: ProjectEventMessage | null | undefined, conn?: ServerConnection | null): void;
  reloadRemoteActive(): Promise<void>;
  /** The server's original bytes, else the http(s) source (CORS); null when neither. */
  fetchRemoteOriginal(conn: ServerConnection, remoteId: string, src: string | null | undefined): Promise<Blob | null>;
  adoptServerFilter(layout: ProjectLayout): void;
  adoptServerPageFormat(layout: ProjectLayout): void;
  adoptServerFormulas(layout: ProjectLayout): void;
  /** Version-guarded save-back; a 409 merges the peer's lines and retries. null when not linked. */
  saveToServer(): Promise<RemoteLink | null>;
  renderResultBytes(): Promise<Uint8Array | null>;
}
