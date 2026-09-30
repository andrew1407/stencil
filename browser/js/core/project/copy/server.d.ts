// A copy made on the source's SERVER: a new project from the original's bytes, its layout saved
// back version-guarded, and its chat file for a whole-project copy. Nothing is linked.
import type { ProjectTransferController } from '../transferController.js';
import type { RemoteLink } from '../../remote/syncController.js';
import type { CopyScope } from './options.js';
import type { CopySource } from './source.js';

/** The server's project names as rows, for the copy-name numbering; [] when unreachable. */
export declare const serverNames: (c: ProjectTransferController, address: string) => Promise<{ id: string; name: string }[]>;
export declare const createServerCopy: (c: ProjectTransferController, src: CopySource, what: CopyScope, name: string) => Promise<RemoteLink>;
