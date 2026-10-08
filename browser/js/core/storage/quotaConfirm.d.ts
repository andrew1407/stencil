// A save's IndexedDB commit, confirmed after the synchronous upsert: an asynchronous quota failure
// resumes the quota ladder, and a payload that never committed takes its registry row with it.
import type { Storage } from './storage.js';
import type { QuotaRung } from './quotaWriter.js';

/** true once the save is in IndexedDB; false when it failed or a newer save superseded it. */
export declare const confirmCommit: (io: Storage, id: string | null, rungs: Generator<QuotaRung, void, void> | null) => Promise<boolean>;
