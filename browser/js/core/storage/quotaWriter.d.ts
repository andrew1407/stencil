// Writing a project under a storage quota: progressively harder JPEG compression, an
// expired-project sweep, eviction of the oldest OTHER project, then a lines-only fallback.
import type { Storage } from './storage.js';
import type { ProjectMeta, ProjectPayload } from '../project/store/projectsStore.js';

/** One upsert attempt of the ladder; `linesOnly` marks the last resort. */
export interface QuotaRung { meta: ProjectMeta; payload: ProjectPayload; note: string; linesOnly?: true; }

/** The ladder's attempts in order; the generator advances only after a quota failure. */
export declare const quotaRungs: (io: Storage, meta: ProjectMeta, payload: ProjectPayload) => Generator<QuotaRung, void, void>;
export declare const showRungSaved: (io: Storage, rung: QuotaRung) => void;
export declare const showStorageFull: (io: Storage, err: unknown) => void;
export declare const isQuotaError: (err: unknown) => boolean;
/** `io` is the Storage instance; returns the ladder at the rung that took, null once it is spent. */
export declare const upsertWithQuota: (io: Storage, meta: ProjectMeta, payload: ProjectPayload) => Generator<QuotaRung, void, void> | null;
