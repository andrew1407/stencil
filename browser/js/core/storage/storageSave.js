// Writing the ACTIVE project: what blocks a save, and the one payload write — through the
// quota-retrying upsert, which puts the payload key down before the registry row.
import { shouldPersist } from '../project/projectsStore.js';
import { getSyncToServer } from '../../net/connectionStore.js';
import { upsertWithQuota } from './quotaWriter.js';
import { buildLayoutState, buildProjectMeta } from '../project/projectMeta.js';

// Nothing persists when sync is off on a fetched server project, or in a temporary editor.
export const saveBlockedReason = (storage) =>
  (storage.app.remoteLink && !getSyncToServer()) ? 'Sync off — not saved'
    : (shouldPersist(storage.activeId, storage.temporary) ? null : 'Temporary — not saved');

export const writeActiveProject = (storage) => {
  const layout = buildLayoutState(storage.app);
  const prev = storage.store.getMeta(storage.activeId) || {};
  // The row keeps its last thumbnail; a fresh one renders in idle time (thumbnail.js).
  const meta = buildProjectMeta(storage.app, { prev, id: storage.activeId, layout, thumbnail: prev.thumbnail ?? null });
  upsertWithQuota(storage, meta, { image: storage.app.imageDataUrl || null, layout });
  storage.thumbs.schedule(storage.activeId);
};
