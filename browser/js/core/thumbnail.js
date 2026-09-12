// ── The projects-list thumbnail ─────────────────────────────────
// It is ALSO the hover-zoom source (projectsModal magnifies it ~1.67x), so it is sized for
// that, not for the 56px row — and it lives in the localStorage registry, so it stays a
// JPEG and stays modest. The pixel work is worker/imageRaster.js (halving + encode).
import { downscaleInline, downscaleToDataUrl } from '../worker/imageTasks.js';

const THUMB_MAX_PX = 480;
// 0.6 put visible JPEG blocking on faces at this size; 0.85 is where that stops.
const THUMB_QUALITY = 0.85;
const OPTS = { maxEdge: THUMB_MAX_PX, type: 'image/jpeg', quality: THUMB_QUALITY, halve: true };

// The EDITED result (filter + lines), so the projects list previews what the user drew.
const source = (app) => (app.image ? app.renderResultCanvas() : null);

// Synchronous, inline: what a save that cannot wait (flush, unload) renders.
export const makeThumbnail = (app) => {
  try {
    const src = source(app);
    return src ? downscaleInline(src, src.width, src.height, OPTS) : null;
  } catch {
    return null;
  }
};

// The same render through the image worker (inline when it is unavailable).
export const renderThumbnail = async (app) => {
  try {
    const src = source(app);
    return src ? await downscaleToDataUrl(src, src.width, src.height, OPTS) : null;
  } catch {
    return null;
  }
};
