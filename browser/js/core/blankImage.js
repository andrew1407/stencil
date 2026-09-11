// ── Blank projects: a solid-colour raster you can recolour in place ──
// Extracted from drawingApp.js. The blob is generated on a scratch canvas and handed to the
// normal load path, so a blank behaves like any other image (lines are a separate overlay).
import { notify } from '../utils.js';
import { normalizeHex } from './accents.js';
import { defaultBlankSizePx } from './layout.js';
import { requireConnection } from '../net/remoteSync.js';
import { PROJECT_ACTION } from '../worker/messages.js';
import constants from '../config/constants.json' with { type: 'json' };
const { PAGE_SIZES } = constants;

// Generate a solid-colour PNG blob of size w×h — the raster backing a blank project. Shared by
// createBlankImage (new blank) and setBlankColor (recolour an existing blank in place).
const blankFillBlob = (w, h, color) => {
  const cnv = document.createElement('canvas');
  cnv.width = w; cnv.height = h;
  const ctx = cnv.getContext('2d');
  ctx.fillStyle = color || '#ffffff';
  ctx.fillRect(0, 0, w, h);
  return new Promise((resolve, reject) => {
    cnv.toBlob(blob => (blob ? resolve(blob) : reject(new Error('Could not create the image'))), 'image/png');
  });
};

// Create a solid-color blank image and load it (shared with the console API). width/height
// in px (clamped 1–8192); omitted → current page size. `address` also creates+links the
// project on that server, validated up front. Resolves { width, height } once handed off.
export const createBlankImage = (app, { color = '#ffffff', width, height, address } = {}) => {
  if (app.storage.incognito) address = undefined;   // incognito never creates on a server
  if (address) requireConnection(app.connections, address);
  const dims = (width != null && height != null)
    ? { width, height }
    : defaultBlankSizePx(app.pageSize === 'custom'
      ? { width: app.customPageWidth, height: app.customPageHeight }
      : (PAGE_SIZES[app.pageSize] || PAGE_SIZES.A4));
  const w = Math.max(1, Math.min(8192, Math.round(dims.width)));
  const h = Math.max(1, Math.min(8192, Math.round(dims.height)));
  const fill = normalizeHex(color) || '#ffffff';
  // A blank's colour IS the page: a filter left over from the previous image
  // would repaint the fill (bw of a red page is flat gray), so start clean.
  if (app.imageFilter !== 'none') app.settings.setImageFilter('none');
  // blankColor marks this as a (recolourable) blank project; it's persisted into project meta.
  return blankFillBlob(w, h, fill).then(blob => {
    app.loadImageFromFile(new File([blob], `blank-${w}x${h}.png`, { type: 'image/png' }),
      { address: address || undefined, blankColor: fill });
    return { width: w, height: h };
  });
};

// True while the active session is a blank project (recolourable solid background).
export const activeIsBlank = (app) => !!app.blankColor;

// Recolour the ACTIVE blank project's background, keeping every drawn line. Regenerates the
// fill at the current dimensions in place, persists blank/blankColor, updates the registry +
// peer tabs, and pushes to a server-linked project. No-op unless this is a blank image.
export const setBlankColor = (app, color) => {
  if (!activeIsBlank(app) || !app.image) return this;
  const next = normalizeHex(color);
  if (!next || next === app.blankColor) return this;
  const w = app.canvas.width, h = app.canvas.height;
  blankFillBlob(w, h, next).then(blob => {
    app.loadImageFromFile(new File([blob], `blank-${w}x${h}.png`, { type: 'image/png' }),
      { replaceInPlace: true, keepAnnotations: true, blankColor: next, keepZoom: true });
    if (app.activeProjectId != null) {
      app.storage.store.setBlankColor(app.activeProjectId, next);
      app.tabs.projectsChanged({ id: app.activeProjectId, action: PROJECT_ACTION.UPDATED });
      app.projectTransfer.pushProjectFieldToServer(app.activeProjectId, { blankColor: next }, 'Could not set blank color on the server');
    }
    app.updateButtons();
  }).catch(() => notify('Could not recolor the blank image', 'fail'));
  return this;
};

