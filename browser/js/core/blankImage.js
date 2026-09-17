// Blank projects: a solid-colour raster generated on a scratch canvas and handed to the
// normal load path, so a blank behaves like any other image.
import { notify } from '../utils.js';
import { normalizeHex } from './accents.js';
import { defaultBlankSizePx } from './layout.js';
import { cropAspect, centeredCrop, isAlbumOrientation } from './cropGeometry.js';
import { requireConnection } from '../net/remoteSync.js';
import { PROJECT_ACTION } from '../worker/messages.js';
import constants from '../config/constants.json' with { type: 'json' };
const { PAGE_SIZES } = constants;

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

// width/height in px (clamped 1–8192); omitted → current page size. `address` also
// creates+links the project on that server.
export const createBlankImage = (app, { color = '#ffffff', width, height, address } = {}) => {
  if (app.storage.incognito) address = undefined;   // incognito never creates on a server
  if (address) requireConnection(app.connections, address);
  const page = app.pageSize === 'custom'
    ? { width: app.customPageWidth, height: app.customPageHeight }
    : (PAGE_SIZES[app.pageSize] || PAGE_SIZES.A4);
  const dims = (width != null && height != null) ? { width, height } : defaultBlankSizePx(page);
  const rw = Math.max(1, Math.min(8192, Math.round(dims.width)));
  const rh = Math.max(1, Math.min(8192, Math.round(dims.height)));
// The load path crops every image to the page aspect, so shape the raster that way here:
// the name then states the size the blank keeps, and no pixel is filled to be cropped off.
  const fit = centeredCrop(rw, rh, cropAspect(page.width, page.height, isAlbumOrientation(rw, rh)));
  const w = Math.max(1, Math.round(fit.width));
  const h = Math.max(1, Math.round(fit.height));
  const fill = normalizeHex(color) || '#ffffff';
// A filter left over from the previous image would repaint the fill, so start clean.
  if (app.imageFilter !== 'none') app.settings.setImageFilter('none');
// blankColor marks a recolourable blank; persisted into project meta.
  return blankFillBlob(w, h, fill).then(blob => {
    app.loadImageFromFile(new File([blob], `blank-${w}x${h}.png`, { type: 'image/png' }),
      { address: address || undefined, blankColor: fill });
    return { width: w, height: h };
  });
};

export const activeIsBlank = (app) => !!app.blankColor;

// Recolour the active blank in place, keeping every drawn line; no-op unless a blank.
export const setBlankColor = (app, color) => {
  if (!activeIsBlank(app) || !app.image) return;
  const next = normalizeHex(color);
  if (!next || next === app.blankColor) return;
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
};

