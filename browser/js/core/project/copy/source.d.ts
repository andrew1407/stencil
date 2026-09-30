// What a copy is taken from, as one shape: a registry row + payload with its image as a data URL,
// plus the server it is linked to — the live editor, a stored row, or a server-only row.
import type { DrawingApp } from '../../drawingApp.js';
import type { ProjectTransferController, RemoteProjectMeta } from '../transferController.js';
import type { ProjectMeta, ProjectLayout } from '../store/projectsStore.js';

export interface CopySource {
  /** The local row whose saved chat travels with a whole-project copy; null for none. */
  srcId: string | null;
  meta: Partial<ProjectMeta>;
  payload: { image: string | null; layout: Partial<ProjectLayout> };
  /** A server row's original as fetched, so a copy onto the server skips decoding the data URL. */
  blob?: Blob;
  remote: { address: string; remoteId: string } | null;
}

/** The live editor as a registry row + payload, its saved row's meta under what is on screen. */
export declare const liveProjectSnapshot: (app: DrawingApp) => Omit<CopySource, 'remote'>;
/** `id` null, or the active project's own id, reads the live editor. */
export declare const readCopySource: (c: ProjectTransferController, id?: string | null) => Promise<CopySource>;
export declare const readRemoteSource: (c: ProjectTransferController, row: RemoteProjectMeta) => Promise<CopySource>;
