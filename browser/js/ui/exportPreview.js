// ── Alt-hover export preview ──────────────────────────────────────────────
// Hovering a copy/download-image variant row while holding Alt shows a small
// rendered thumbnail of exactly what that action would produce, in a floating
// tooltip that follows the cursor. Reused by the context menu's Copy/Download
// Image flyouts and the toolbar's copy/download options popovers.
//
// ── The preview is sand too (js/ui/motion.js surfaceIn/surfaceOut) ─────────
// It forms from motes streaming out of the row it previews and comes apart into
// motes pouring back into it, on the shared short tip clock: Alt-hover sweeps rows
// fast, so a flight has to be over before the next one begins — and a SWAP (the
// thumbnail changing under a held Alt) replays the same gather rather than
// snapping, exactly like landing on a new row.
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

// ── Alt tracking, decoupled from mousemove ──────────────────────────────────
// Alt is a KEY, not a pointer event — reading e.altKey off mousemove only learns
// the modifier changed once the pointer moves again, so pressing/releasing Alt
// while parked on a row did nothing until the next rehover. One pair of window
// listeners (installed once, however many rows call wireAltPreview) tracks the
// key itself; `hovered` is which row the pointer is over right now, kept current
// by that row's own mouseenter/mousemove/mouseleave.
let altHeld = false;
let hovered = null;    // { item, app, variant, point } for the row under the pointer
let lastX = 0;
let lastY = 0;
let globalListenersReady = false;

// `fromCursor`: a KEY-triggered appearance (Alt pressed) forms from the cursor
// itself; a pointer-driven one (gliding onto a row with Alt already held) keeps
// forming from the row's centre.
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
    // Released Alt pours the preview back into the CURSOR, mirroring the press;
    // the pointer-driven hides (mouseleave, blur) keep aiming at the row's centre.
    hideExportPreview({ x: lastX, y: lastY });
  });
  // Alt-tabbing (or any other way focus leaves the window) never delivers the
  // matching keyup — without this the preview, and the held-down state, would
  // be stuck showing after the user has moved on to another window entirely.
  window.addEventListener('blur', () => {
    if (!altHeld) return;
    altHeld = false;
    hideExportPreview();
  });
};

// Forget which row is hovered, e.g. when the menu that owns it closes — a stale
// `hovered` would otherwise re-show a removed row's preview on the next Alt press.
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
  // A freshly-set data: URL still decodes asynchronously — the FIRST time this
  // tip is ever shown, place()'s getBoundingClientRect() runs before the image
  // has a real size, so the box lays out tiny/wrong and (with no dust flight
  // playable at that size either) is easy to miss entirely. Re-place once decode
  // actually lands; a no-op on every later, already-cached show.
  img.addEventListener('load', () => {
    if (tip.classList.contains('ctx-preview-visible')) place(lastX, lastY);
  });
  tip.appendChild(img);
  document.body.appendChild(tip);
  return tip;
};

// Render `variant` ('current' | 'original' | 'tint' | 'split') onto a small
// offscreen canvas via the app's own export renderer, downscaled for a quick, cheap
// preview — this runs on every Alt+hover, not just on click. Each row hands its OWN
// literal variant (the split row previews the split composite, the current row
// previews the plain edit — the two are independent rows now, not one that swaps).
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

// Rendered thumbs, one per variant, kept while the owning menu is open: a full-res
// render per re-hovered row was the cost. Cleared when the menu closes
// (clearAltPreviewHover) and whenever the app changes.
const thumbCache = new Map();
let thumbApp = null;
const thumbFor = (app, variant) => {
  if (thumbApp !== app) { thumbCache.clear(); thumbApp = app; }
  let url = thumbCache.get(variant);
  if (!url) { url = renderThumbDataUrl(app, variant); thumbCache.set(variant, url); }
  return url;
};

const place = (x, y) => placeNearCursor(tip, x, y);

// Show (or move) the preview for `variant`, anchored near the pointer (x, y).
// `point` is the row's own centre — where this appearance's dust streams from —
// and stays null for a caller with no row geometry handy (the dust just declines).
// `dustFrom` overrides only THIS appearance's origin (an Alt press forms from the
// cursor); `point` still names the row for later, pointer-driven hides.
// No-ops quietly if there's no image loaded or the variant fails to render.
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
  // An APPEARANCE (first show, or a swap onto a different variant) forms out of the
  // row — or out of `dustFrom` when a key, not the pointer, asked for it; a re-point
  // that never left the same row leaves the flight alone. (surfaceIn itself settles
  // the tip when there is no origin.)
  if (changed || !wasVisible) surfaceIn(t, dustFrom || curPoint, { ms: DUST_IN_MS });
}

// `dustTo` overrides where the leave pours into (Alt released = the cursor);
// omitted, it aims at the row the preview was last shown for.
export function hideExportPreview(dustTo = null) {
  if (!tip) return;
  const wasVisible = tip.classList.contains('ctx-preview-visible');
  // Photographed and dusted while it is still the box on screen — the cloud is what
  // it leaves behind, so the hand-over is one beat, not a cut.
  if (wasVisible) surfaceOut(tip, dustTo || curPoint, { ms: DUST_OUT_MS });
  else settleSurface(tip);
  tip.classList.remove('ctx-preview-visible');
  curVariant = null;
  curApp = null;
  curPoint = null;
}

// Wire one menu/popover row: holding Alt while the pointer is over `item` shows
// the live preview for `variant`; releasing Alt, or leaving the row, hides it —
// whichever of the two (key or pointer) changes last, not just a rehover.
export function wireAltPreview(item, app, variant) {
  ensureGlobalAltListeners();
  // The row's centre only needs reading once per hover (the menu doesn't reflow
  // under a stationary pointer) — cache it here instead of on every mousemove.
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
