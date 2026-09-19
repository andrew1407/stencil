// Writing the ACTIVE project: what blocks a save, and the one payload write — through the
// quota-retrying upsert, which puts the payload key down before the registry row.
import type { Storage } from './storage.js';

/** The hint to show instead of saving, or null when the write may go ahead. */
export declare const saveBlockedReason: (storage: Storage) => string | null;
/** Builds the layout + meta from live app state, upserts them and schedules the thumbnail. */
export declare const writeActiveProject: (storage: Storage) => void;
