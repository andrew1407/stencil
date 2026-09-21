// Writing a project under a storage quota: progressively harder JPEG compression, an
// expired-project sweep, eviction of the oldest OTHER project, then a lines-only fallback.
import type { Storage } from './storage.js';
import type { ProjectMeta, ProjectPayload } from '../project/projectsStore.js';

/** `io` is the Storage instance (store / activeId / app / showImageMissingBanner). */
export declare const upsertWithQuota: (io: Storage, meta: ProjectMeta, payload: ProjectPayload) => void;
