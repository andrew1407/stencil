// `aspect` is injected — the page format the rect is locked to belongs to the controls.
import { centeredCrop, moveCropClamped, resizeCropFromCorner, roundRect, scaleCropCentered }
  from '../lib/cropGeometry.js';

const VIEWPORT_PAD = 10;   // keep in sync with .viewport padding in crop.css

export const createCropStage = ({ state, aspect }) => {
  const viewport = document.getElementById('viewport');
  const imgEl = document.getElementById('image');
  const overlay = document.getElementById('overlay');
  const cropBox = document.getElementById('crop-box');
  const previewCanvas = document.getElementById('preview');
  const cropInfo = document.getElementById('crop-info');
  const masks = {
    top: overlay.querySelector('.mask-top'),
    bottom: overlay.querySelector('.mask-bottom'),
    left: overlay.querySelector('.mask-left'),
    right: overlay.querySelector('.mask-right')
  };

  const resetCrop = () => {
    state.crop = roundRect(centeredCrop(state.imgW, state.imgH, aspect()), state.imgW, state.imgH);
    layoutOverlay();
    renderPreview();
  };

  const fitToWindow = () => {
    // clientWidth/Height include the padding, so subtract both gutters (plus a small fudge).
    const vw = viewport.clientWidth - VIEWPORT_PAD * 2 - 4;
    const vh = (viewport.clientHeight - VIEWPORT_PAD * 2 - 4) || Math.round(window.innerHeight * 0.72);
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
    // Wheel over the crop rect scales it FROM ITS CENTRE (core scaleCropCentered); elsewhere it
    // zooms the view. A trackpad pinch is a ctrl+wheel event in Chromium, so it flows through too.
    const box = cropBox.getBoundingClientRect();
    const overBox = e.clientX >= box.left && e.clientX <= box.right &&
                    e.clientY >= box.top && e.clientY <= box.bottom;
    if (overBox) {
      const factor = Math.pow(1.0015, -e.deltaY);   // wheel up / pinch out → grow
      state.crop = roundRect(scaleCropCentered(state.crop, factor, aspect(), state.imgW, state.imgH), state.imgW, state.imgH);
      // Re-anchor an in-progress drag so the next pointer-move doesn't snap the size back.
      if (drag) { drag.startCrop = { ...state.crop }; drag.start = toImageSpace(e.clientX, e.clientY); }
      layoutOverlay();
      renderPreview();
    } else {
      setZoom(state.zoom * (e.deltaY < 0 ? 1.12 : 0.89), e.clientX, e.clientY);
    }
  }, { passive: false });

  // The viewport reaches its real size a moment after the modal iframe settles: re-fit on
  // resize, but only at the default fit (zoom === 1) so a manual zoom is never clobbered.
  new ResizeObserver(() => { if (state.imgW && state.zoom === 1) { fitToWindow(); layoutOverlay(); } }).observe(viewport);

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

  return { imgEl, overlay, fitToWindow, resetCrop, layoutOverlay, renderPreview };
};
