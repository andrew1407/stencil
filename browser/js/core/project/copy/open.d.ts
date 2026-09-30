// Where a finished copy goes: nowhere, this tab, or a new one — a local row by its id, a server
// copy by its link (never a token), an unsaved incognito copy by its bytes.
import type { ProjectTransferController } from '../transferController.js';
import type { RemoteLink } from '../../remote/syncController.js';
import type { CopyOpen } from './options.js';
import type { ProjectMeta, ProjectLayout } from '../store/projectsStore.js';

/** `target` is the new local id, or the server copy's link when `onServer`. */
export declare const openSavedCopy: (c: ProjectTransferController, opts: { open: CopyOpen; onServer: boolean },
  target: string | RemoteLink, name: string, win?: Window | null) => Promise<void>;
/** Opens an unsaved copy here or in a new tab; a new tab refuses an image past LAUNCH_DATA_URL_MAX. */
export declare const openIncognitoCopy: (c: ProjectTransferController, open: Exclude<CopyOpen, 'none'>,
  copy: { meta: Partial<ProjectMeta>; payload: { image: string; layout: Partial<ProjectLayout> } },
  name: string, win?: Window | null) => void;
