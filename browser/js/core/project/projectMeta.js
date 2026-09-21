// Live app → plain state readers: buildLayoutState feeds the pure serializeSession();
// buildProjectMeta builds the registry row (the projects list reads only this, never the payload).
import { serializeSession, buildLayoutPayload } from '../layout.js';
import { layoutLineLengthCm } from '../units.js';
import { addPeriod, DEFAULT_PERIOD } from './projectsStore.js';
import { makeThumbnail } from '../image/thumbnail.js';

export const buildLayoutState = (app) => {
  const viewport = document.getElementById('canvas-viewport');
  const state = {
    imageWidth: app.canvas.width,
    imageHeight: app.canvas.height,
    // Rotated-image pixels; the stored image stays the untouched original.
    cropRect: app.cropRect,
    // 0..3 clockwise quarter-turns applied to the original before the crop is taken.
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
    // Provenance: the image's own URL and the page it came from; empty for local uploads.
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

// The export subset: what the server push and ExportService's download/copy both send.
export const currentLayoutPayload = (app) => buildLayoutPayload({
  imageWidth: app.canvas.width,
  imageHeight: app.canvas.height,
  lines: app.lines,
  imageFilter: app.imageFilter,
  filterColor: app.filterColor,
  cropRect: app.cropRect,
  rotationQuarters: app.rotationQuarters,
  pageSize: app.pageSize,
  customPageWidth: app.customPageWidth,
  customPageHeight: app.customPageHeight,
  allowFormulas: app.allowFormulas,
  formulaX: app.formulaX,
  formulaY: app.formulaY,
});

export const buildProjectMeta = (app, { prev = {}, id, layout, thumbnail = makeThumbnail(app) }) => ({
  id,
  name: prev.name || app.imageBaseName || 'Untitled',
  // Preserved across saves (each set only via its own setter); "" / [] = none.
  color: prev.color || '',
  description: prev.description || '',
  keywords: Array.isArray(prev.keywords) ? [...prev.keywords] : [],
  // Non-empty colour ⇔ blank project.
  blank: !!app.blankColor,
  blankColor: app.blankColor || '',
  // Opened from a .stencil file (drives the bronze projects-list outline).
  fromFile: !!app.fromFile,
  thumbnail,
  createdAt: prev.createdAt ?? Date.now(),
  // Expiration is owned by the expiration modal / open-time snap; a plain edit carries it forward.
  expiresAt: prev.expiresAt ?? addPeriod(Date.now(), DEFAULT_PERIOD),
  refreshPeriod: prev.refreshPeriod ?? DEFAULT_PERIOD,
  autoRefresh: prev.autoRefresh ?? true,
  hasImage: !!app.imageDataUrl,
  imageW: app.canvas.width,
  imageH: app.canvas.height,
  // Cached (cm) so the projects-list tooltip needn't reload the payload to measure it.
  lineLengthCm: layoutLineLengthCm(layout),
  // Mirrored so the projects list and the extension "resume" match need no payload.
  source: app.imageSource || null,
  resource: app.imageResource || null,
  // Server linkage, so the projects list shows this row as the SAME project as its remote row.
  address: app.remoteLink?.address || null,
  remoteId: app.remoteLink?.remoteId || null,
  // Last-known server version (LWW guard), so reopening restores the link without a stale-version conflict.
  remoteVersion: app.remoteLink?.version || 0,
});
