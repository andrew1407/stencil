// ── What a saved project is made of ─────────────────────────────
// Extracted from storage.js: the live app → plain state readers. buildLayoutState feeds the
// pure serializeSession(); buildProjectMeta builds the registry row (the projects list reads
// only this, never the image-heavy payload). Both read the app, neither writes it.
import { serializeSession } from './layout.js';
import { layoutLineLengthCm } from './units.js';
import { addPeriod, DEFAULT_PERIOD } from './projectsStore.js';
import { makeThumbnail } from './thumbnail.js';

// Reads the live app + viewport into a plain state object (the DOM/DrawingApp
// coupling), then hands it to the pure serializeSession().
export const buildLayoutState = (app) => {
  const viewport = document.getElementById('canvas-viewport');
  const state = {
    imageWidth: app.canvas.width,
    imageHeight: app.canvas.height,
    // The crop rectangle (rotated-image pixels). The stored image stays the
    // untouched original; the rotation + crop are re-applied on load.
    cropRect: app.cropRect,
    // 90° quarter-turns (0..3, clockwise) applied to the original before the
    // crop is taken. cropRect lives in this rotated space.
    rotationQuarters: app.rotationQuarters || 0,
    lines: app.lines,
    pageSize: app.pageSize,
    customPageWidth: app.customPageWidth,
    customPageHeight: app.customPageHeight,
    unit: app.unit,
    color: app.color,
    pointColor: app.pointColor,
    thickness: app.thickness,
    pointSize: app.pointSize,
    style: app.style,
    showPoints: app.showPoints,
    showLines: app.showLines,
    imageFilter: app.imageFilter,
    filterColor: app.filterColor,
    zoom: app.scale,
    scrollLeft: viewport ? viewport.scrollLeft : 0,
    scrollTop: viewport ? viewport.scrollTop : 0,
    imageBaseName: app.imageBaseName || null,
    imageExt: app.imageExt || null,
    // Provenance: the image/video's own URL (source) and the web page it was
    // pulled from (resource). Both empty for plain local uploads; populated by
    // the add-by-URL flow and the browser extension hand-off.
    imageSource: app.imageSource || null,
    imageResource: app.imageResource || null,
    tooltipEnabled: app.tooltipEnabled,
    tooltipShowPage: app.tooltipShowPage,
    tooltipShowScreen: app.tooltipShowScreen,
    tooltipShowCoords: app.tooltipShowCoords,
    allowFormulas: app.allowFormulas,
    formulaX: app.formulaX,
    formulaY: app.formulaY,
    drawMode: app.drawMode,
    holdDrawDelay: app.holdDrawDelay,
    selGlowColor: app.selGlowColor,
    hoverRingColor: app.hoverRingColor,
    focusRingColor: app.focusRingColor,
    defaultFillColor: app.defaultFillColor,
  };
  return serializeSession(state);
};

export const buildProjectMeta = (app, { prev = {}, id, layout }) => ({
  id,
  name: prev.name || app.imageBaseName || 'Untitled',
  // Accent colour and description are preserved across saves (each set only via its own
  // setter); "" means none — the theme fallback, and no description.
  color: prev.color || '',
  description: prev.description || '',
  // Blank-image flag + fill colour, sourced from the active session (set at blank creation
  // and by setBlankColor, restored on open). Non-empty colour ⇔ blank project; "" / false for
  // ordinary image projects.
  blank: !!app.blankColor,
  blankColor: app.blankColor || '',
  // Provenance: opened from a portable .stencil file (drives the bronze projects-list
  // outline). Sourced from the active session like blank/blankColor above.
  fromFile: !!app.fromFile,
  thumbnail: makeThumbnail(app),
  createdAt: prev.createdAt ?? Date.now(),
  // Expiration is owned by the expiration modal / open-time snap; a plain edit
  // just carries the existing values forward (new projects default to a week).
  expiresAt: prev.expiresAt ?? addPeriod(Date.now(), DEFAULT_PERIOD),
  refreshPeriod: prev.refreshPeriod ?? DEFAULT_PERIOD,
  autoRefresh: prev.autoRefresh ?? true,
  hasImage: !!app.imageDataUrl,
  imageW: app.canvas.width,
  imageH: app.canvas.height,
  // Cached real-world length of all drawn lines (cm), computed from the same layout so
  // the projects-list tooltip needn't reload the image-heavy payload to measure it.
  lineLengthCm: layoutLineLengthCm(layout),
  // Mirror provenance into the registry meta so the projects list and the
  // extension-launch "resume" match can use it without reading the payload.
  source: app.imageSource || null,
  resource: app.imageResource || null,
  // Server linkage (set when this session was opened from / pushed to a server),
  // so the projects list can show this row as the SAME project as its golden
  // remote row instead of a duplicate. Null for purely-local projects.
  address: app.remoteLink?.address || null,
  remoteId: app.remoteLink?.remoteId || null,
  // Last-known server version (LWW guard), so reopening a server-linked project
  // from the list restores the link without a stale-version save conflict.
  remoteVersion: app.remoteLink?.version || 0,
});
