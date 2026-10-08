// Writing a project under a storage quota: sheds a data-URL source's text, sweeps expired projects,
// evicts the oldest OTHER project, tries progressively harder JPEG compression, and finally falls
// back to lines-only. `io` is the Storage instance (store / activeId / app / showImageMissingBanner).
import { shedSource } from '../project/store/projectSources.js';

const QUALITIES = [null, 0.85, 0.65, 0.45, 0.25]; // null = original

// The ladder as rungs, each one upsert attempt; the next rung runs only after a quota failure.
// Every rung but the last keeps the image; `linesOnly` is the last resort.
export const quotaRungs = function* (io, meta, payload) {
  const baseImage = payload.image;
  let body = payload;
  const rung = (img, note) => ({ meta: { ...meta, hasImage: !!img }, payload: { ...body, image: img }, note });
  for (const q of QUALITIES) {
    const img = (baseImage == null || q === null) ? baseImage : compressImage(io, q);
    if (baseImage != null && q !== null && !img) continue;
    let swept = false;
    for (;;) {
      yield rung(img, q === null || baseImage == null ? '' : ` (compressed ${Math.round(q * 100)}%)`);
      if (body === payload && (body = shedSource(payload)) !== payload) continue;
      if (!swept) {
        io.store.sweepExpired(Date.now());
        swept = true;
        continue;
      }
      // Then evict the oldest OTHER project (never the active one).
      if (evictOldestOther(io)) {
        try {
          io.app.tabs?.projectsChanged();
        } catch {
          /* coordinator gone — cross-tab sync is best-effort; eviction still happened */
        }
        continue;
      }
      break; // nothing left to evict at this quality → try lower quality
    }
  }
  yield { ...rung(null, ''), meta: { ...meta, hasImage: false }, linesOnly: true };
};

export const showRungSaved = (io, rung) => {
  if (rung.linesOnly) {
    io.app.showSaveStatus('Lines saved — image too large for browser storage', 'var(--warning)', 'alert');
    io.showImageMissingBanner(true);
    console.warn('Project image too large for storage; saved layout only.');
    return;
  }
  io.app.showSaveStatus('Saved' + rung.note, 'var(--success)', 'check');
  io.showImageMissingBanner(false);
};

export const showStorageFull = (io, e) => {
  console.warn('Could not save project:', e);
  io.app.showSaveStatus('Save failed (storage full)', 'var(--danger)', 'x');
};

// Upsert with progressive image compression + eviction on quota exhaustion. Returns the ladder
// positioned at the rung that took, so an asynchronous quota failure resumes it; null once spent.
export const upsertWithQuota = (io, meta, payload) => {
  const rungs = quotaRungs(io, meta, payload);
  for (let step = rungs.next(); !step.done; step = rungs.next()) {
    try {
      io.store.upsert(step.value.meta, step.value.payload);
      showRungSaved(io, step.value);
      return step.value.linesOnly ? null : rungs;
    } catch (e) {
      if (step.value.linesOnly) { showStorageFull(io, e); return null; }
      if (!isQuotaError(e)) throw e;
    }
  }
  return null;
};

export const isQuotaError = (e) => {
  return e && (e.name === 'QuotaExceededError' || e.name === 'NS_ERROR_DOM_QUOTA_REACHED' || e.code === 22);
};

// Remove the least-recently-updated project that is NOT the active one.
// Returns true if something was evicted.
const evictOldestOther = (io) => {
  const others = io.store.list().filter(m => m.id !== io.activeId);
  if (others.length === 0) return false;
  // list() is sorted updatedAt desc → oldest is last.
  const oldest = others[others.length - 1];
  io.store.remove(oldest.id);
  return true;
};

// Operates on the ORIGINAL (full) image, never the cropped view — the crop is persisted
// separately as a rectangle and re-applied on load.
const compressImage = (io, quality) => {
  const src = io.app.originalImage || io.app.image;
  if (!src) return null;
  try {
    const offscreen = document.createElement('canvas');
    offscreen.width = src.width;
    offscreen.height = src.height;
    offscreen.getContext('2d').drawImage(src, 0, 0);
    return offscreen.toDataURL('image/jpeg', quality);
  } catch {
    return null;
  }
};
