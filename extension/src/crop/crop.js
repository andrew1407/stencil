// ── Quick crop page ─────────────────────────────────────────────────────────
// The editor's crop model: a rect in ORIGINAL-image pixels whose aspect is locked to the
// page (any ISO A/B/C format, or custom). The stage, the controls and the editor hand-off
// live beside this file; what is left is the load, the rotate and the boot.
import { cropAspect, isAlbumOrientation, pageDims } from '../lib/cropGeometry.js';
import { fetchAsDataUrl, filenameFromUrl, getSettings, openEditorTab, CROP_SRC_KEY, CROP_META_KEY } from '../lib/stencil.js';
import { SRC } from '../lib/messages.js';
import { watchNumericInputs } from '../lib/numericInput.js';
import { initTooltips } from '../lib/controlTooltip.js';
import { wireScrollbarHover } from '../lib/scrollbarHover.js';
import { enhanceSelect } from '../lib/customSelect.js';
import { createCropStage } from './cropStage.js';
import { createCropControls } from './cropControls.js';
import { buildHandoffPayload } from './cropHandoff.js';

// True when running inside the in-page crop modal (an iframe). We then notify the
// host overlay when we booted (so it keeps the modal) and when to close.
const FRAMED = window.parent && window.parent !== window;

const postToHost = (type) => {
  if (FRAMED) window.parent.postMessage({ source: SRC.MODAL, type }, '*');
};

// Answered AS SOON AS this script runs: the host's watchdog asks "did the frame load at
// all?" (a CSP / mixed-content block stops it dead), not "did the image finish loading".
// The later ready (below) stays; the host handles it idempotently.
postToHost('ready');

const statusEl = document.getElementById('status');

const state = {
  srcUrl: '',
  source: '',
  resource: '',
  dataUrl: '',
  name: 'image.png',
  imgW: 0,
  imgH: 0,
  page: 'A3',
  customW: 21,
  customH: 29.7,
  album: true,
  crop: { x: 0, y: 0, width: 0, height: 0 },
  fitScale: 1,
  zoom: 1
};

// ── Page aspect ──
const aspect = () => {
  const d = pageDims(state.page, state.customW, state.customH);
  return cropAspect(d.width, d.height, state.album);
};

const { imgEl, overlay, fitToWindow, resetCrop, layoutOverlay } = stage;
const { syncPageControls, syncOrientationButtons, onCustom } = createCropControls({ state, resetCrop });

const init = async () => {
  if (!state.srcUrl) {
    statusEl.textContent = 'No image URL provided.';
    return;
  }
  state.name = filenameFromUrl(state.srcUrl);
  try {
    state.page = (await getSettings()).page || 'A3';
  } catch {
    /* default */
  }
  syncPageControls();
  try {
    // state.resource (the page the image came from, via CROP_META) lends its host.
    state.dataUrl = await fetchAsDataUrl(state.srcUrl, { pageUrl: state.resource });
  } catch (err) {
    statusEl.textContent = `Could not load the image (${err.message}).`;
    return;
  }

  imgEl.onload = () => {
    // An animated GIF keeps cycling frames (distracting while positioning the box).
    // Freeze it to the current frame by baking onto a canvas and swapping in that static
    // PNG; the reload re-enters onload, this time as a non-GIF.
    if (!state.frozen && /^data:image\/gif/i.test(state.dataUrl)) {
      state.frozen = true;
      const c = document.createElement('canvas');
      c.width = imgEl.naturalWidth || 1;
      c.height = imgEl.naturalHeight || 1;
      c.getContext('2d').drawImage(imgEl, 0, 0);
      state.dataUrl = c.toDataURL('image/png');
      imgEl.src = state.dataUrl;
      return;
    }
    state.imgW = imgEl.naturalWidth;
    state.imgH = imgEl.naturalHeight;
    state.album = isAlbumOrientation(state.imgW, state.imgH);
    syncOrientationButtons();
    fitToWindow();
    resetCrop();
    overlay.hidden = false;
    statusEl.textContent = '';
    postToHost('ready');   // confirm to the host overlay that the frame loaded
  };
  imgEl.onerror = () => { statusEl.textContent = 'The image failed to decode.'; };
  imgEl.src = state.dataUrl;
  window.addEventListener('resize', () => {
    fitToWindow();
    layoutOverlay();
  });
};

// ── Rotate ──
// Baked into the source image, so the crop coords the editor gets match the rotated
// picture. Dimensions swap, so orientation and the centred rect follow.
const rotate = (clockwise) => {
  if (!state.imgW) return;
  const c = document.createElement('canvas');
  c.width = state.imgH;
  c.height = state.imgW;
  const ctx = c.getContext('2d');
  ctx.translate(c.width / 2, c.height / 2);
  ctx.rotate((clockwise ? 1 : -1) * Math.PI / 2);
  ctx.drawImage(imgEl, -state.imgW / 2, -state.imgH / 2);
  state.dataUrl = c.toDataURL('image/png');
  state.imgW = c.width;
  state.imgH = c.height;
  state.album = isAlbumOrientation(state.imgW, state.imgH);
  syncOrientationButtons();
  imgEl.onload = () => {
    fitToWindow();
    resetCrop();
    overlay.hidden = false;
  };
  imgEl.src = state.dataUrl;
};

document.getElementById('rotate-left').addEventListener('click', () => rotate(false));
document.getElementById('rotate-right').addEventListener('click', () => rotate(true));
window.addEventListener('keydown', (e) => {
  if (!e.altKey || e.ctrlKey || e.metaKey) return;
  if (e.key === 'r' || e.key === 'R') {
    e.preventDefault();
    rotate(e.shiftKey);   // Alt+R = left, Alt+Shift+R = right
  }
});
document.getElementById('custom-w').addEventListener('input', onCustom);
document.getElementById('custom-h').addEventListener('input', onCustom);
document.getElementById('reset').addEventListener('click', resetCrop);

document.getElementById('open').addEventListener('click', async (e) => {
  const btn = e.currentTarget;
  btn.disabled = true;
  try {
    const incognito = document.getElementById('incognito').checked;
    const mode = document.querySelector('input[name="mode"]:checked').value;
    const payload = buildHandoffPayload(state, imgEl, { mode, incognito });
    await openEditorTab(payload);   // full editor always opens in a new tab
    statusEl.textContent = 'Opened in editor.';
    postToHost('close');            // dismiss the quick-crop modal
  } catch (err) {
    statusEl.textContent = `Failed to open: ${err.message}`;
  } finally {
    btn.disabled = false;
  }
});

// ── Bootstrap (last, so every const above is defined before init runs) ──
// Image source comes from session storage (set by launchCrop); fall back to ?src.
(async () => {
  let src = new URLSearchParams(location.search).get('src') || '';
  if (!src) {
    try { const d = await chrome.storage.session.get(CROP_SRC_KEY); src = d[CROP_SRC_KEY] || ''; }
    catch { /* leave empty → "No image URL provided." */ }
  }
  state.srcUrl = src;
  // Provenance set by launchCrop (the image's own URL + the page it came from), so
  // the post-crop editor hand-off keeps where the image came from. Empty otherwise.
  try {
    const m = await chrome.storage.session.get(CROP_META_KEY);
    state.source = (m[CROP_META_KEY] && m[CROP_META_KEY].source) || '';
    state.resource = (m[CROP_META_KEY] && m[CROP_META_KEY].resource) || '';
  } catch {
    state.source = '';
    state.resource = '';
  }
  // Custom page W/H take an expression — "45 + 9", "* 2".
  watchNumericInputs();
  init();
})();

// Instant, structured tooltips (the native `title` waits ~1s and dies on a disabled
// control); lib/tipContent.js gives them their shape.
initTooltips();
wireScrollbarHover();   // the crop stage's bars take the accent under the pointer

// The page-format list is long — our own list gets the filter input and the theme.
for (const el of document.querySelectorAll('select')) enhanceSelect(el, { search: true });
