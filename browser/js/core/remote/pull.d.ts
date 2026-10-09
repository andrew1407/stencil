// The pull half of a co-edit session: a peer's new original fetched and loaded over the stored
// layout, the peer's rename adopted through the store. The in-place layout adoption is
// peerLayout.js; the version-guarded writes are push.js.
import type { DrawingApp } from '../drawingApp.js';
import type { ServerConnection, RemoteProjectFull, ProjectEventMessage } from '../../net/serverConnection.js';
import type { RemoteLink } from './syncController.js';

export declare class RemotePull {
  constructor(app: DrawingApp);
  app: DrawingApp;
  /** Loads the server's original (else its http(s) source) with the stored layout; false when neither
   * could be fetched, or `live` says the editor left the project while the download was out. */
  reloadPicture(conn: ServerConnection, link: RemoteLink, full: RemoteProjectFull, live: () => boolean): Promise<boolean>;
  /** A peer's rename lands in the store directly, so it is not echoed back. */
  adoptPeerName(peerName: string | null | undefined): void;
  /** After a feed drop: the linked project's record, handed to `judge` as an `updated` event. */
  resumed(conn: ServerConnection | null | undefined, judge: (msg: ProjectEventMessage) => void): Promise<void>;
  /** The server's original bytes, else the http(s) source (CORS); null when neither. */
  fetchRemoteOriginal(conn: ServerConnection, remoteId: string, src: string | null | undefined): Promise<Blob | null>;
}
