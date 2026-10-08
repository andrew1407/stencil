// A save's IndexedDB commit, confirmed after the synchronous upsert: an asynchronous quota failure
// resumes the quota ladder (quotaWriter.js) where the upsert left it, and a project whose payload
// never committed loses the registry row it would orphan. Nothing here blocks the save itself.
import { PROJECT_PREFIX } from '../project/store/projectsStore.js';
import { IMAGE_PREFIX } from '../project/store/projectImages.js';
import { isQuotaError, showRungSaved, showStorageFull } from './quotaWriter.js';

const failureOf = (backend, id) =>
  backend.writeFailure(PROJECT_PREFIX + id) || backend.writeFailure(IMAGE_PREFIX + id);

const giveUp = (io, backend, id, err) => {
  if (isQuotaError(err)) showStorageFull(io, err);
  if (backend.isCommitted(PROJECT_PREFIX + id) || !io.store.getMeta(id)) return false;
  io.store.remove(id);
  try { io.app.tabs?.projectsChanged(); } catch { /* cross-tab sync is best-effort */ }
  return false;
};

// The next rung that the store takes synchronously, or null when the ladder is spent.
const nextRung = (io, rungs) => {
  for (let step = rungs.next(); !step.done; step = rungs.next()) {
    try {
      io.store.upsert(step.value.meta, step.value.payload);
      showRungSaved(io, step.value);
      return step.value;
    } catch (e) {
      if (step.value.linesOnly || !isQuotaError(e)) return null;
    }
  }
  return null;
};

// true once the save is in IndexedDB; false when it failed, or a newer save superseded it.
export const confirmCommit = async (io, id, rungs) => {
  const backend = io.backend;
  if (id == null || typeof backend?.writeFailure !== 'function') return true;
  const seq = (io.commitSeq = (io.commitSeq || 0) + 1);
  for (;;) {
    await backend.flush();
    if (io.commitSeq !== seq) return false;
    const err = failureOf(backend, id);
    if (!err) return true;
    if (!rungs || !isQuotaError(err)) return giveUp(io, backend, id, err);
    const took = nextRung(io, rungs);
    if (!took) return giveUp(io, backend, id, err);
    if (took.linesOnly) rungs = null;
  }
};
