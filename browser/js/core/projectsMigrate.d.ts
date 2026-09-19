// The one-shot migration of the legacy single-project keys into the registry, guarded by
// MIGRATED_FLAG; the legacy keys themselves are never deleted.
import type { ProjectsStore, StorageBackend } from './projectsStore.js';

/** The new project id, or null when the flag was set or there was nothing to migrate. */
export declare const migrateLegacyProject: (
  store: ProjectsStore,
  storage: StorageBackend,
  migratedFlag: string,
  now: number,
) => string | null;
