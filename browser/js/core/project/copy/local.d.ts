// A copy saved as a new LOCAL project: the scoped row + payload, the source's thumbnail when the
// copy shows what it shows, and its chat for a whole-project copy.
import type { ProjectTransferController } from '../transferController.js';
import type { CopyScope } from './options.js';
import type { CopySource } from './source.js';

/** Resolves to the new project's id. */
export declare const createLocalCopy: (c: ProjectTransferController, src: CopySource, what: CopyScope, name: string) => Promise<string>;
