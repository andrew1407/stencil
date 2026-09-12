// Alt-hover export preview: a thumbnail of exactly what a copy/download variant would
// produce, following the cursor. It forms from motes streaming out of the row it previews
// and pours back into it on the short tip clock; a swap under a held Alt replays the gather.
import { surfaceIn, surfaceOut, settleSurface, rectCenter,
         TIP_DUST_IN_MS, TIP_DUST_OUT_MS } from './motion.js';
import { placeNearCursor } from '../utils.js';

const PREVIEW_MAX = 220; // px, longest edge of the rendered thumbnail
const DUST_IN_MS = TIP_DUST_IN_MS;
const DUST_OUT_MS = TIP_DUST_OUT_MS;

let tip = null;
let curVariant = null;
let curApp = null;
let curPoint = null;   // the row's own centre — the dust's origin/destination

// Alt is a key, not a pointer event: one pair of window listeners tracks it, and `hovered`
// is the row under the pointer, kept current by the row's own mouse events.
let altHeld = false;
let hovered = null;    // { item, app, variant, point } for the row under the pointer
let lastX = 0;
let lastY = 0;
let globalListenersReady = false;

// A key-triggered appearance forms from the cursor; a pointer-driven one from the row's centre.
const showHovered = (fromCursor = false) => {
  if (!hovered) return;
  showExportPreview(hovered.app, hovered.variant, lastX, lastY, hovered.point,
    fromCursor ? { x: lastX, y: lastY } : null);
};

const ensureGlobalAltListeners = () => {
  if (globalListenersReady || typeof window === 'undefined') return;
  globalListenersReady = true;
  window.addEventListener('keydown', (e) => {
    if (e.key !== 'Alt' || altHeld) return;
    altHeld = true;
    showHovered(/*fromCursor=*/true);
  });
  window.addEventListener('keyup', (e) => {
    if (e.key !== 'Alt') return;
    altHeld = false;
// Released Alt pours the preview back into the cursor, mirroring the press.
    hideExportPreview({ x: lastX, y: lastY });
  });
// Focus leaving the window never delivers the matching keyup.
  window.addEventListener('blur', () => {
    if (!altHeld) return;
    altHeld = false;
    hideExportPreview();
  });
};

// Called when the owning menu closes; a stale `hovered` would re-show a removed row.
export function clearAltPreviewHover() {
  hovered = null;
  thumbCache.clear();   // the menu is gone — the canvas may change before the next one
}

const ensureTip = () => {
  if (tip) return tip;
  tip = document.createElement('div');
  tip.id = 'ctx-preview-tip';
  tip.className = 'ctx-preview-tip';
  const img = document.createElement('img');
  img.alt = '';
// A fresh data: URL decodes asynchronously: the first show lays out before the image has a
// size, so re-place once decode lands (a no-op on later, cached shows).
  img.addEventListener('load', () => {
    if (tip.classList.contains('ctx-preview-visible')) place(lastX, lastY);
  });
  tip.appendChild(img);
  document.body.appendChild(tip);
  return tip;
};

// Render `variant` onto a small offscreen canvas via the app's export renderer, downscaled;
// this runs on every Alt+hover. Each row previews its own literal variant.
const renderThumbDataUrl = (app, variant) => {
  const off = app.export.renderExportCanvas(variant);
  const scale = Math.min(1, PREVIEW_MAX / Math.max(off.width, off.height));
  const w = Math.max(1, Math.round(off.width * scale));
  const h = Math.max(1, Math.round(off.height * scale));
  const thumb = document.createElement('canvas');
  thumb.width = w;
  thumb.height = h;
  thumb.getContext('2d').drawImage(off, 0, 0, w, h);
  return thumb.toDataURL('image/png');
};

// One rendered thumb per variant while the owning menu is open; cleared when it closes
// and whenever the app changes.
const thumbCache = new Map();
let thumbApp = null;
const thumbFor = (app, variant) => {
  if (thumbApp !== app) { thumbCache.clear(); thumbApp = app; }
  let url = thumbCache.get(variant);
  if (!url) { url = renderThumbDataUrl(app, variant); thumbCache.set(variant, url); }
  return url;
};

const place = (x, y) => placeNearCursor(tip, x, y);

// `point` is the row's centre (where the dust streams from; null declines the dust);
// `dustFrom` overrides only this appearance's origin. No-op without an image.
export function showExportPreview(app, variant, x, y, point = null, dustFrom = null) {
  if (!app?.image) return;
  const t = ensureTip();
  const changed = curVariant !== variant || curApp !== app;
  if (changed) {
    try {
      t.querySelector('img').src = thumbFor(app, variant);
    } catch {
      hideExportPreview();
      return;
    }
    curVariant = variant;
    curApp = app;
  }
  const wasVisible = t.classList.contains('ctx-preview-visible');
  t.classList.add('ctx-preview-visible');
  lastX = x;
  lastY = y;
  place(x, y);
  curPoint = point;
// An appearance (first show or a variant swap) forms out of the row; a re-point on the same
// row leaves the flight alone.
  if (changed || !wasVisible) surfaceIn(t, dustFrom || curPoint, { ms: DUST_IN_MS });
}

// `dustTo` overrides where the leave pours into (Alt released = the cursor).
export function hideExportPreview(dustTo = null) {
  if (!tip) return;
  const wasVisible = tip.classList.contains('ctx-preview-visible');
// Photographed while still on screen, so the hand-over is one beat, not a cut.
  if (wasVisible) surfaceOut(tip, dustTo || curPoint, { ms: DUST_OUT_MS });
  else settleSurface(tip);
  tip.classList.remove('ctx-preview-visible');
  curVariant = null;
  curApp = null;
  curPoint = null;
}

// Holding Alt over `item` shows the preview for `variant`; releasing Alt or leaving hides it.
export function wireAltPreview(item, app, variant) {
  ensureGlobalAltListeners();
// The row's centre is read once per hover; the menu doesn't reflow under a stationary pointer.
  item.addEventListener('mouseenter', e => {
    hovered = { item, app, variant, point: rectCenter(item) };
    lastX = e.clientX;
    lastY = e.clientY;
    if (altHeld) showHovered();
  });
  item.addEventListener('mousemove', e => {
    lastX = e.clientX;
    lastY = e.clientY;
    if (altHeld) showHovered();
  });
  item.addEventListener('mouseleave', () => {
    if (hovered?.item === item) hovered = null;
    hideExportPreview();
  });
}
