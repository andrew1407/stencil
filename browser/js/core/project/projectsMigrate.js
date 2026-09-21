// The one-shot migration of the legacy single-project keys into the registry. Guarded by
// MIGRATED_FLAG, so repeated calls no-op; the legacy keys themselves are never deleted.
import { addPeriod, DEFAULT_PERIOD } from './projectPeriods.js';
import { readJSON } from './projectRegistryIo.js';

const LEGACY_IMAGE_KEY = 'drawingApp_image';
const LEGACY_LAYOUT_KEY = 'drawingApp_layout';

// Returns the new project id, or null when there was nothing to migrate.
export const migrateLegacyProject = (store, storage, migratedFlag, now) => {
  if (storage.getItem(migratedFlag)) return null;

  const image = storage.getItem(LEGACY_IMAGE_KEY) || null;
  const layout = readJSON(storage, LEGACY_LAYOUT_KEY, null);

  if (image == null && layout == null) {
    storage.setItem(migratedFlag, '1');
    return null;
  }

  const safeLayout = (layout && typeof layout === 'object') ? layout : {};
  const id = store.createId();
  const meta = {
    id,
    name: safeLayout.imageBaseName || 'Untitled 1',
    color: '',
    description: '',
    thumbnail: null,
    createdAt: now,
    updatedAt: now,
    expiresAt: addPeriod(now, DEFAULT_PERIOD),
    refreshPeriod: DEFAULT_PERIOD,
    autoRefresh: true,
    hasImage: !!image,
    imageW: safeLayout.imageWidth || null,
    imageH: safeLayout.imageHeight || null,
    // Filled on the first real save once page metrics are live.
    lineLengthCm: 0,
  };
  store.upsert(meta, { image, layout: safeLayout });
  // Pin to the requested `now` so the migrated timestamps are deterministic.
  store.touch(id, now);
  storage.setItem(migratedFlag, '1');
  return id;
};
