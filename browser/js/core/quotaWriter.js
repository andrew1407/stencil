// ── Writing a project under a storage quota ─────────────────────
// Extracted from storage.js. Tries progressively harder JPEG compression, sweeps expired
// projects, then evicts the oldest OTHER project, and finally falls back to lines-only.
// `io` is the Storage instance (store / activeId / app / showImageMissingBanner).

// Upsert with progressive image compression + eviction on quota exhaustion.
export const upsertWithQuota = (io, meta, payload) => {
  const qualities = [null, 0.85, 0.65, 0.45, 0.25]; // null = original
  const baseImage = payload.image;

  const tryStore = imageField => {
    const m = { ...meta };
    const p = { ...payload, image: imageField };
    m.hasImage = !!imageField;
    io.store.upsert(m, p);
  };

  // 1) Try each compression level; on quota, sweep expired then evict oldest
  //    OTHER projects one at a time and retry.
  for (const q of qualities) {
    const img = (baseImage == null || q === null) ? baseImage : compressImage(io, q);
    if (baseImage != null && q !== null && !img) continue;
    let attempted = false;
    while (true) {
      try {
        tryStore(img);
        const note = q === null || baseImage == null ? '' : ` (compressed ${Math.round(q * 100)}%)`;
        io.app.showSaveStatus('Saved' + note, 'var(--success)', 'check');
        io.showImageMissingBanner(false);
        return;
      } catch (e) {
        if (!isQuotaError(e)) throw e;
        if (!attempted) {
          // First quota hit at this quality: reclaim expired projects.
          io.store.sweepExpired(Date.now());
          attempted = true;
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
  }

  // 2) Final fallback: store with no image so at least the lines survive.
  try {
    const m = { ...meta, hasImage: false };
    io.store.upsert(m, { ...payload, image: null });
    io.app.showSaveStatus('Lines saved — image too large for browser storage', 'var(--warning)', 'alert');
    io.showImageMissingBanner(true);
    console.warn('Project image too large for storage; saved layout only.');
  } catch (e) {
    console.warn('Could not save project:', e);
    io.app.showSaveStatus('Save failed (storage full)', 'var(--danger)', 'x');
  }
};

const isQuotaError = (e) => {
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

// Compress image to JPEG at given quality; returns data URL or null. Operates
// on the ORIGINAL (full) image so the stored image is never the cropped view —
// the crop is persisted separately as a rectangle and re-applied on load.
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
