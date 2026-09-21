// ── The projects-list thumbnail ─────────────────────────────────
// It is ALSO the hover-zoom source (projectsModal magnifies it ~1.67x), so it is sized for
// that, not for the 56px row — and it lives in the localStorage registry, so it stays a
// JPEG and stays modest. The pixel work is worker/imageRaster.js (halving + encode).
import { downscaleInline, downscaleToDataUrl } from '../../worker/imageTasks.js';

const THUMB_MAX_PX = 480;
// 0.6 put visible JPEG blocking on faces at this size; 0.85 is where that stops.
const THUMB_QUALITY = 0.85;
const OPTS = { maxEdge: THUMB_MAX_PX, type: 'image/jpeg', quality: THUMB_QUALITY, halve: true };
// An idle slot usually comes within a frame; past this the render runs regardless, so a
// thumbnail always lands before the 400ms cross-tab "project updated" broadcast.
const IDLE_TIMEOUT_MS = 300;

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

const requestIdle = (fn) => (typeof requestIdleCallback === 'function'
  ? { idle: requestIdleCallback(fn, { timeout: IDLE_TIMEOUT_MS }) }
  : { timer: setTimeout(fn, 0) });
const cancelIdle = (h) => (h.idle != null ? cancelIdleCallback(h.idle) : clearTimeout(h.timer));

// A burst of saves renders one thumbnail in idle time; flush() renders inline, because a
// project switch or an unload cannot wait for an idle slot.
export const createThumbnailScheduler = (io, { idle = requestIdle, cancel = cancelIdle } = {}) => {
  let handle = null;
  let id = null;
  let gen = 0;   // bumped by every schedule/flush: an in-flight render older than it is dropped

  const land = (projectId, url) => {
    if (!url || io.activeId !== projectId) return;
    io.store.setThumbnail(projectId, url);
    try { io.app.tabs?.projectsChanged(); } catch { /* cross-tab refresh is best-effort */ }
  };

  const run = async () => {
    handle = null;
    const projectId = id, mine = ++gen;
    if (io.activeId !== projectId) return;
    const url = await renderThumbnail(io.app);
    if (mine === gen) land(projectId, url);
  };

  return {
    schedule(projectId) {
      id = projectId;
      if (handle) cancel(handle);
      handle = idle(run);
    },
    flush() {
      if (!handle) return;
      cancel(handle);
      handle = null;
      gen++;
      land(id, makeThumbnail(io.app));
    },
    pending: () => handle != null,
  };
};
