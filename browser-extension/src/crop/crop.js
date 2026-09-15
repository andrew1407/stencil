// Quick crop page: a rect in ORIGINAL-image pixels, aspect locked to the page format. The
// stage, the controls and the editor hand-off live beside this file; here: load, rotate, boot.
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

// Inside the in-page crop modal (an iframe): notify the host overlay on boot and on close.
const FRAMED = window.parent && window.parent !== window;

const postToHost = (type) => {
  if (FRAMED) window.parent.postMessage({ source: SRC.MODAL, type }, '*');
};

// Answered as soon as this script runs: the host's watchdog asks "did the frame load at
// all?" (a CSP / mixed-content block stops it dead), not "did the image finish loading".
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

const aspect = () => {
  const d = pageDims(state.page, state.customW, state.customH);
  return cropAspect(d.width, d.height, state.album);
};

const { imgEl, overlay, fitToWindow, resetCrop, layoutOverlay } = createCropStage({ state, aspect });
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
    // An animated GIF keeps cycling while positioning: bake the current frame onto a canvas and
    // swap in that static PNG; the reload re-enters onload, this time as a non-GIF.
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

// Rotate is baked into the source image, so the crop coords the editor gets match the picture.
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

// Bootstrap last, so every const above is defined. Source: session storage (launchCrop), else ?src.
(async () => {
  let src = new URLSearchParams(location.search).get('src') || '';
  if (!src) {
    try { const d = await chrome.storage.session.get(CROP_SRC_KEY); src = d[CROP_SRC_KEY] || ''; }
    catch { /* leave empty → "No image URL provided." */ }
  }
  state.srcUrl = src;
  // Provenance from launchCrop, so the post-crop editor hand-off keeps where the image came from.
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

// Instant tooltips: the native `title` waits ~1 s and dies on a disabled control.
initTooltips();
wireScrollbarHover();   // the crop stage's bars take the accent under the pointer

for (const el of document.querySelectorAll('select')) enhanceSelect(el, { search: true });
