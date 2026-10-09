// Live co-edit push/pull + server writes: the debounced save-back, the peer-event reload,
// the conflict-merge retry loop and the server layout-adoption helpers. Holds only sync
// timing state; the session link, connections and the editor model live on the app.
import type { DrawingApp } from '../drawingApp.js';
import type { ProjectLayout } from '../project/store/projectsStore.js';
import type { ServerConnection, ProjectEventMessage, FeedResumedMessage } from '../../net/serverConnection.js';

/** The editor's link to a server project; version is the save-back guard. */
export interface RemoteLink { address: string; remoteId: string; version: number; }

export declare class RemoteSyncController {
  constructor(app: DrawingApp);
  app: DrawingApp;
  /** Debounced layout push after an edit; the rendered result trails it (resultUpload.js). */
  scheduleRemoteSync(): void;
  /** A peer's project-event; a resumed feed re-reads the linked project and judges it as one. */
  onServerProjectEvent(msg: ProjectEventMessage | FeedResumedMessage | null | undefined, conn?: ServerConnection | null): void;
  /** Records the loaded picture's server record; only one with an originalHash lets a peer's layout edit in place. */
  noteServerImage(rec: { hasImage?: boolean; originalHash?: string; blankColor?: string } | null | undefined): void;
  /** Records this editor's own upload as the server's original, keeping the noted blank fill. */
  noteOwnOriginal(hash: string): void;
  /** The layout alone when both records carry the same originalHash and the geometry is unchanged, else a full reload. */
  reloadRemoteActive(): Promise<void>;
  /** Leaving the project: drops the pending push and pull, forgets the picture, and makes every
   * round trip still out drop its reply (Storage.loadProject, newTemporary). */
  detach(): void;
  /** Uploads a pending result now, rendered from what is on screen (project switch, unload). */
  flushResult(): void;
  /** The server's original bytes, else the http(s) source (CORS); null when neither. */
  fetchRemoteOriginal(conn: ServerConnection, remoteId: string, src: string | null | undefined): Promise<Blob | null>;
  adoptServerFilter(layout: ProjectLayout): void;
  adoptServerPageFormat(layout: ProjectLayout): void;
  adoptServerFormulas(layout: ProjectLayout): void;
  /** Explicit version-guarded save of the layout, then the result; a 409 merges and retries. null when not linked. */
  saveToServer(): Promise<RemoteLink | null>;
  renderResultBytes(): Promise<Uint8Array | null>;
}
