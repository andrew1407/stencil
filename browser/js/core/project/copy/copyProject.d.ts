// "Make a copy": the one path every entry point takes (desktop twin: app/project/copy/ProjectCopy.cpp)
// — read the source, name it "<name>-copy(N)", save it locally or on its server, then open it.
import type { ProjectTransferController, RemoteProjectMeta } from '../transferController.js';
import type { CopyProjectRequest } from './options.js';

export interface CopyProjectCall extends CopyProjectRequest {
  /** A stored row; null (the default) is the live editor. */
  id?: string | null;
  /** A server-only listing row instead of a local one. */
  remote?: RemoteProjectMeta | null;
  /** A tab opened inside the user's gesture, for `open: 'newtab'`. */
  win?: Window | null;
}

/** The new local id, the server copy's remote id, or null for an unsaved incognito copy. */
export declare const copyProject: (c: ProjectTransferController, call: CopyProjectCall) => Promise<string | null>;
