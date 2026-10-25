// Writing the ACTIVE project: what blocks a save, and the one payload write — through the
// quota-retrying upsert, which puts the payload key down before the registry row, then the
// confirmation that it committed to IndexedDB.
import { shouldPersist } from '../project/store/projectsStore.js';
import { getSyncToServer } from '../../net/connectionStore.js';
import { upsertWithQuota } from './quotaWriter.js';
import { confirmCommit } from './quotaConfirm.js';
import { buildLayoutState, buildProjectMeta } from '../project/meta/projectMeta.js';
import { rawJson } from '../project/store/projectRegistryIo.js';
import { cursorStep } from '../historyStack.js';

// Nothing persists when sync is off on a fetched server project, or in a temporary editor.
export const saveBlockedReason = (storage) =>
  (storage.app.remoteLink && !getSyncToServer()) ? 'Sync off — not saved'
    : (shouldPersist(storage.activeId, storage.temporary) ? null : 'Temporary — not saved');

// The lines' JSON, kept for a view-only save (zoom, scroll) of the same project, lines and undo
// step; every edit saves in full after it, so the text a view save reuses is never left on disk.
const savedLines = new WeakMap();
const linesText = (storage, viewOnly) => {
  const { lines, history } = storage.app;
  const key = { id: storage.activeId, lines, step: history ? cursorStep(history) : null };
  const last = savedLines.get(storage);
  if (viewOnly && last && last.id === key.id && last.lines === key.lines && last.step === key.step) return last.text;
  const text = JSON.stringify(lines);
  savedLines.set(storage, { ...key, text });
  return text;
};

export const writeActiveProject = (storage, { viewOnly = false } = {}) => {
  const layout = buildLayoutState(storage.app);
  const prev = storage.store.getMeta(storage.activeId) || {};
  // The row keeps its last thumbnail; a fresh one renders in idle time (thumbnail.js).
  const meta = buildProjectMeta(storage.app, { prev, id: storage.activeId, layout, thumbnail: prev.thumbnail ?? null });
  const stored = Array.isArray(layout.lines) ? { ...layout, lines: rawJson(linesText(storage, viewOnly)) } : layout;
  const rungs = upsertWithQuota(storage, meta, { image: storage.app.imageDataUrl || null, layout: stored });
  storage.thumbs.schedule(storage.activeId);
  return confirmCommit(storage, storage.activeId, rungs);
};
