// The promise KV over the IndexedDB object store that holds the project payloads, images and
// thumbnails; a write resolves when its transaction commits.
import type { PayloadKv } from './projectsBackend.js';

/** Null when IndexedDB is missing. */
export declare const createIdbKv: (idb?: IDBFactory | null) => PayloadKv | null;
