// ── Quick crop page ─────────────────────────────────────────────────────────
// Mirrors the editor's crop model: a rect in ORIGINAL-image pixels whose aspect is
// locked to the page (A3/A4/custom). Drag to move, resize corner-only, scroll to
// zoom. Then "Keep original" (full image + crop rect) or "Cut cropped part" (bake
// the region into a new image), opening the editor in a tab.
import {
  cropAspect, centeredCrop, resizeCropFromCorner, moveCropClamped,
  roundRect, isAlbumOrientation, pageDims
} from '../lib/cropGeometry.js';
import { fetchAsDataUrl, filenameFromUrl, getSettings, openEditorTab, CROP_SRC_KEY } from '../lib/stencil.js';

// True when running inside the in-page crop modal (an iframe). We then notify the
// host overlay when we booted (so it keeps the modal) and when to close.
const FRAMED = window.parent && window.parent !== window;

const postToHost = (type) => {
  if (FRAMED) window.parent.postMessage({ source: 'stencil-modal', type }, '*');
};

const viewport = document.getElementById('viewport');
const imgEl = document.getElementById('image');
const overlay = document.getElementById('overlay');
const cropBox = document.getElementById('crop-box');
const previewCanvas = document.getElementById('preview');
const statusEl = document.getElementById('status');
const cropInfo = document.getElementById('crop-info');
const masks = {
  top: overlay.querySelector('.mask-top'),
  bottom: overlay.querySelector('.mask-bottom'),
  left: overlay.querySelector('.mask-left'),
  right: overlay.querySelector('.mask-right')
};

const state = {
  srcUrl: '',
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
  syncPageButtons();
  try {
    state.dataUrl = await fetchAsDataUrl(state.srcUrl);
  } catch (err) {
    statusEl.textContent = `Could not load the image (${err.message}).`;
    return;
  }

  imgEl.onload = () => {
    // An animated GIF keeps cycling frames inside the crop view (and the preview
    // canvas), which is distracting while positioning the box. Freeze it to the
    // frame on screen right now by baking it onto a canvas and swapping in that
    // static PNG; the reload re-enters onload, this time as a non-GIF.
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

// ── Page aspect ──
const aspect = () => {
  const d = pageDims(state.page, state.customW, state.customH);
  return cropAspect(d.width, d.height, state.album);
};

const resetCrop = () => {
  state.crop = roundRect(centeredCrop(state.imgW, state.imgH, aspect()), state.imgW, state.imgH);
  layoutOverlay();
  renderPreview();
};

// ── Zoom ──
const fitToWindow = () => {
  const vw = viewport.clientWidth - 4;
  const vh = viewport.clientHeight - 4 || Math.round(window.innerHeight * 0.72);
  state.fitScale = Math.min(vw / state.imgW, vh / state.imgH) || 1;
  state.zoom = 1;
  applyZoom();
};

const displayScale = () => state.fitScale * state.zoom;

const applyZoom = () => {
  const s = displayScale();
  imgEl.style.width = `${state.imgW * s}px`;
  imgEl.style.height = `${state.imgH * s}px`;
  document.getElementById('zoom-label').textContent = `${Math.round(s * 100)}%`;
  layoutOverlay();
};

const setZoom = (nextZoom, cx, cy) => {
  const clamped = Math.max(0.1, Math.min(nextZoom, 16 / state.fitScale));
  // Keep the image point under the cursor stable, when a cursor is given.
  let anchor = null;
  if (cx != null) {
    const r = imgEl.getBoundingClientRect();
    anchor = { ix: (cx - r.left) / displayScale(), iy: (cy - r.top) / displayScale(), cx, cy };
  }
  state.zoom = clamped;
  applyZoom();
  if (anchor) {
    const vr = viewport.getBoundingClientRect();
    viewport.scrollLeft = anchor.ix * displayScale() - (anchor.cx - vr.left);
    viewport.scrollTop = anchor.iy * displayScale() - (anchor.cy - vr.top);
  }
};

document.getElementById('zoom-in').addEventListener('click', () => setZoom(state.zoom * 1.25));
document.getElementById('zoom-out').addEventListener('click', () => setZoom(state.zoom * 0.8));
document.getElementById('zoom-fit').addEventListener('click', fitToWindow);
viewport.addEventListener('wheel', (e) => {
  if (!state.imgW) return;
  e.preventDefault();
  setZoom(state.zoom * (e.deltaY < 0 ? 1.12 : 0.89), e.clientX, e.clientY);
}, { passive: false });

// ── Overlay layout (image-space → display px) ──
const scale = () => (imgEl.getBoundingClientRect().width / state.imgW) || 1;

const layoutOverlay = () => {
  const s = scale();
  const c = state.crop;
  const left = c.x * s;
  const top = c.y * s;
  const w = c.width * s;
  const h = c.height * s;
  const W = state.imgW * s;
  const H = state.imgH * s;
  Object.assign(cropBox.style, { left: `${left}px`, top: `${top}px`, width: `${w}px`, height: `${h}px` });
  Object.assign(masks.top.style, { left: 0, top: 0, width: `${W}px`, height: `${top}px` });
  Object.assign(masks.bottom.style, { left: 0, top: `${top + h}px`, width: `${W}px`, height: `${H - top - h}px` });
  Object.assign(masks.left.style, { left: 0, top: `${top}px`, width: `${left}px`, height: `${h}px` });
  Object.assign(masks.right.style, { left: `${left + w}px`, top: `${top}px`, width: `${W - left - w}px`, height: `${h}px` });
  cropInfo.textContent = `${c.width}×${c.height}px from ${state.imgW}×${state.imgH}`;
};

const renderPreview = () => {
  const c = state.crop;
  if (!c.width || !c.height) return;
  const ratio = Math.min(320 / c.width, 320 / c.height, 1);
  previewCanvas.width = Math.max(1, Math.round(c.width * ratio));
  previewCanvas.height = Math.max(1, Math.round(c.height * ratio));
  previewCanvas.getContext('2d').drawImage(imgEl, c.x, c.y, c.width, c.height, 0, 0, previewCanvas.width, previewCanvas.height);
};

// ── Pointer interaction ──
let drag = null;

const toImageSpace = (clientX, clientY) => {
  const r = imgEl.getBoundingClientRect();
  const s = scale();
  return { x: (clientX - r.left) / s, y: (clientY - r.top) / s };
};

cropBox.addEventListener('pointerdown', (e) => {
  const corner = e.target.dataset.corner;
  drag = {
    mode: corner != null ? 'resize' : 'move',
    corner: corner != null ? Number(corner) : null,
    start: toImageSpace(e.clientX, e.clientY),
    startCrop: { ...state.crop }
  };
  e.target.setPointerCapture?.(e.pointerId);
  e.preventDefault();
});

window.addEventListener('pointermove', (e) => {
  if (!drag) return;
  const p = toImageSpace(e.clientX, e.clientY);
  const moved = drag.mode === 'move'
    ? moveCropClamped(drag.startCrop, p.x - drag.start.x, p.y - drag.start.y, state.imgW, state.imgH)
    : resizeCropFromCorner(drag.startCrop, drag.corner, p.x, p.y, aspect(), state.imgW, state.imgH);
  state.crop = roundRect(moved, state.imgW, state.imgH);
  layoutOverlay();
  renderPreview();
});

window.addEventListener('pointerup', () => { drag = null; });

// ── Controls ──
const syncPageButtons = () => {
  document.querySelectorAll('#page-seg button').forEach(b => b.classList.toggle('active', b.dataset.page === state.page));
  document.getElementById('custom-dims').hidden = state.page !== 'custom';
};

const syncOrientationButtons = () => {
  document.querySelectorAll('#orient-seg button').forEach(b => b.classList.toggle('active', (b.dataset.album === 'true') === state.album));
};

const onCustom = () => {
  const w = parseFloat(document.getElementById('custom-w').value);
  const h = parseFloat(document.getElementById('custom-h').value);
  if (w > 0) state.customW = w;
  if (h > 0) state.customH = h;
  if (state.page === 'custom') resetCrop();
};

document.getElementById('page-seg').addEventListener('click', (e) => {
  const b = e.target.closest('button');
  if (!b) return;
  state.page = b.dataset.page;
  syncPageButtons();
  resetCrop();
});
document.getElementById('orient-seg').addEventListener('click', (e) => {
  const b = e.target.closest('button');
  if (!b) return;
  state.album = b.dataset.album === 'true';
  syncOrientationButtons();
  resetCrop();
});

// ── Rotate ──
// Bake a 90° turn into the source image so the crop coords we later hand the editor
// match the rotated picture. Dimensions swap, so orientation follows the new shape
// and the crop re-centers to the page.
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
    const page = state.page === 'custom' ? { size: 'custom', width: state.customW, height: state.customH } : { size: state.page };
    let payload;
    if (mode === 'apply') {
      payload = {
        dataUrl: state.dataUrl,
        name: state.name,
        crop: state.crop,
        page,
        incognito
      };
    } else {
      const c = state.crop;
      const canvas = document.createElement('canvas');
      canvas.width = c.width;
      canvas.height = c.height;
      canvas.getContext('2d').drawImage(imgEl, c.x, c.y, c.width, c.height, 0, 0, c.width, c.height);
      const dot = state.name.lastIndexOf('.');
      payload = {
        dataUrl: canvas.toDataURL('image/png'),
        name: (dot > 0 ? state.name.slice(0, dot) : state.name) + '-crop.png',
        crop: { x: 0, y: 0, width: c.width, height: c.height },
        page,
        incognito
      };
    }
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
  init();
})();
