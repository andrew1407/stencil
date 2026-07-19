import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { notify } from '../utils.js';
import constants from '../config/constants.json' with { type: 'json' };
import { cropAspect, centeredCrop, resizeCropFromCorner, moveCropClamped, scaleCropCentered, cropChange, isAlbumOrientation } from '../core/cropGeometry.js';
import { icon } from './icons.js';
const { PAGE_SIZES } = constants;

// ── Component: image-crop modal ─────────────────────────────────
// Move/resize a crop rect over the original image, locked to the page aspect ratio
// (resizable from corners only; Album/Portrait toggle flips orientation). Confirm
// stores the rect via DrawingApp.applyCrop WITHOUT replacing the original, so it
// stays re-adjustable. Geometry runs in the shared C++ core (cropGeometry.js → wasm)
// so desktop and browser crops match.
export class StencilCropModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal" style="width:auto;max-width:calc(100vw - 32px);">
            <div class="settings-header">
                <h2>${icon('crop', { size: 18 })} Crop Image</h2>
                <button class="app-modal-close btn-icon-text" id="crop-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body" style="display:flex;flex-direction:column;align-items:center;gap:12px;">
                <div id="crop-stage" style="position:relative;display:inline-block;line-height:0;max-width:100%;background:#222;">
                    <img id="crop-image-el" alt="Crop preview" style="display:block;width:auto;height:auto;max-width:calc(96vw - 60px);max-height:calc(82vh - 180px);user-select:none;-webkit-user-drag:none;">
                    <!-- Dimming backdrop lives in its own clip layer so the huge box-shadow is
                         clipped to the image, while the crop box + corner handles below sit in
                         the UNclipped stage (else handles at the image edge get sliced in half). -->
                    <div id="crop-shade-clip" style="position:absolute;inset:0;overflow:hidden;pointer-events:none;">
                        <div id="crop-shade" style="position:absolute;box-shadow:0 0 0 9999px rgba(0,0,0,0.45);display:none;"></div>
                    </div>
                    <div id="crop-box" style="position:absolute;box-sizing:border-box;border:2px solid #4da3ff;cursor:move;display:none;">
                        <span class="crop-handle" data-corner="0" style="position:absolute;width:14px;height:14px;background:#4da3ff;border:2px solid #fff;border-radius:50%;left:-8px;top:-8px;cursor:nwse-resize;"></span>
                        <span class="crop-handle" data-corner="1" style="position:absolute;width:14px;height:14px;background:#4da3ff;border:2px solid #fff;border-radius:50%;right:-8px;top:-8px;cursor:nesw-resize;"></span>
                        <span class="crop-handle" data-corner="2" style="position:absolute;width:14px;height:14px;background:#4da3ff;border:2px solid #fff;border-radius:50%;right:-8px;bottom:-8px;cursor:nwse-resize;"></span>
                        <span class="crop-handle" data-corner="3" style="position:absolute;width:14px;height:14px;background:#4da3ff;border:2px solid #fff;border-radius:50%;left:-8px;bottom:-8px;cursor:nesw-resize;"></span>
                    </div>
                </div>
                <div id="crop-dims" style="font-size:13px;color:var(--text-muted);"></div>
            </div>
            <div class="settings-footer">
                <button id="crop-orientation" class="btn-icon-text" title="Swap album / portrait — flips the crop orientation">${icon('swap', { size: 14 })}<span>Album</span></button>
                <span class="footer-hint">Drag to move · drag a corner to resize (aspect locked to the page).</span>
                <button id="crop-cancel" class="btn-icon-text">${icon('x', { size: 14 })}<span>Cancel</span></button>
                <button id="crop-apply" class="btn-icon-text">${icon('check', { size: 14 })}<span>Apply Crop</span></button>
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-crop-modal', 'id="crop-modal-overlay" class="app-modal-overlay"', StencilCropModal.inner()); }

  wire(app) {
    const overlay = document.getElementById('crop-modal-overlay');
    const img = document.getElementById('crop-image-el');
    const box = document.getElementById('crop-box');
    const shade = document.getElementById('crop-shade');   // clipped dimming backdrop (mirrors box)
    const dims = document.getElementById('crop-dims');
    const orientBtn = document.getElementById('crop-orientation');

    // Working state (all rect math in ORIGINAL-image pixel space).
    let rect = { x: 0, y: 0, width: 0, height: 0 };
    let album = false;
    let aspect = 1;
    let scale = 1;          // display px per image px
    let iw = 0, ih = 0;     // original image dimensions

    // Page natural dimensions (cm), NOT orientation-swapped — only proportions
    // matter. Mirrors blankImageModal's pageDims() (the as-selected page).
    const pageDims = () => (app.pageSize === 'custom'
      ? { width: app.customPageWidth, height: app.customPageHeight }
      : PAGE_SIZES[app.pageSize] || PAGE_SIZES.A4);

    const computeScale = () => {
      const r = img.getBoundingClientRect();
      scale = iw > 0 && r.width > 0 ? r.width / iw : 1;
    };

    const renderBox = () => {
      box.style.display = 'block';
      box.style.left = (rect.x * scale) + 'px';
      box.style.top = (rect.y * scale) + 'px';
      box.style.width = (rect.width * scale) + 'px';
      box.style.height = (rect.height * scale) + 'px';
      // The dimming backdrop tracks the same rect in its clip layer.
      shade.style.display = 'block';
      shade.style.left = box.style.left;
      shade.style.top = box.style.top;
      shade.style.width = box.style.width;
      shade.style.height = box.style.height;
      dims.textContent = `${Math.round(rect.width)} × ${Math.round(rect.height)} px · ${album ? 'Album (landscape)' : 'Portrait'}`;
      orientBtn.innerHTML = icon('swap', { size: 14 }) + `<span>${album ? 'Album' : 'Portrait'}</span>`;
    };

    // Re-fit a centered crop for the current orientation (used on open + on flip).
    const recenter = () => {
      aspect = cropAspect(pageDims().width, pageDims().height, album);
      rect = centeredCrop(iw, ih, aspect);
      renderBox();
    };

    const { open, close } = wireModalShell(overlay, null, document.getElementById('crop-close'), {
      onClose: () => { box.style.display = 'none'; }
    });

    // Open guard: needs a loaded original image.
    const openCrop = () => {
      if (!app.originalImage || !app.imageDataUrl) {
        notify('Open an image first', 'fail');
        return;
      }
      // Preview the rotated original so the crop rect (which lives in rotated
      // pixel space) lines up with what's shown.
      const dims = app.imageModel.effectiveOriginalDims();
      iw = dims.w;
      ih = dims.h;
      // Seed from the current applied crop (or a centered default).
      rect = app.cropRect ? { ...app.cropRect } : centeredCrop(iw, ih, cropAspect(pageDims().width, pageDims().height, isAlbumOrientation(iw, ih)));
      album = isAlbumOrientation(rect.width, rect.height);
      aspect = cropAspect(pageDims().width, pageDims().height, album);
      open();
      // Position once the preview image has its displayed size.
      img.onload = () => { computeScale(); renderBox(); };
      img.src = app.imageModel.effectiveOriginalDataUrl();
      if (img.complete && img.naturalWidth) { computeScale(); renderBox(); }
    };
    document.getElementById('crop-image').addEventListener('click', openCrop);

    // ── Interactive move / corner-resize ──
    // clientX/Y → image-space pixel, relative to the displayed image.
    const toImage = (clientX, clientY) => {
      const r = img.getBoundingClientRect();
      return { x: (clientX - r.left) / scale, y: (clientY - r.top) / scale };
    };

    let drag = null; // { kind: 'move'|'resize', corner, startImg, startRect }
    const onDown = (e, kind, corner) => {
      e.preventDefault();
      e.stopPropagation();
      drag = { kind, corner, startImg: toImage(e.clientX, e.clientY), startRect: { ...rect } };
      document.addEventListener('mousemove', onMove);
      document.addEventListener('mouseup', onUp);
    };
    const onMove = e => {
      if (!drag) return;
      const cur = toImage(e.clientX, e.clientY);
      if (drag.kind === 'move') {
        const dx = cur.x - drag.startImg.x;
        const dy = cur.y - drag.startImg.y;
        rect = moveCropClamped(drag.startRect, dx, dy, iw, ih);
      } else {
        rect = resizeCropFromCorner(drag.startRect, drag.corner, cur.x, cur.y, aspect, iw, ih);
      }
      renderBox();
    };
    const onUp = () => {
      drag = null;
      document.removeEventListener('mousemove', onMove);
      document.removeEventListener('mouseup', onUp);
    };

    box.addEventListener('mousedown', e => onDown(e, 'move'));
    box.querySelectorAll('.crop-handle').forEach(h =>
      h.addEventListener('mousedown', e => onDown(e, 'resize', parseInt(h.dataset.corner, 10))));

    // Mouse wheel / trackpad pinch (a ctrl+wheel event in Chromium) over the crop rect → grow/shrink
    // it from its centre via core scaleCropCentered (aspect locked, clamped). passive:false to preventDefault.
    document.getElementById('crop-stage').addEventListener('wheel', e => {
      if (box.style.display === 'none' || iw <= 0) return;
      // Only when the cursor is INSIDE the crop rect (not just anywhere over the image).
      const b = box.getBoundingClientRect();
      if (e.clientX < b.left || e.clientX > b.right || e.clientY < b.top || e.clientY > b.bottom) return;
      e.preventDefault();
      const factor = Math.pow(1.0015, -e.deltaY);   // wheel up / pinch out → grow
      rect = scaleCropCentered(rect, factor, aspect, iw, ih);
      // If a move/resize drag is underway, re-anchor it to the new size + current cursor so the
      // next mousemove doesn't snap the rect back to its pre-wheel size.
      if (drag) { drag.startRect = { ...rect }; drag.startImg = toImage(e.clientX, e.clientY); }
      renderBox();
    }, { passive: false });

    orientBtn.addEventListener('click', () => { album = !album; recenter(); });
    document.getElementById('crop-cancel').addEventListener('click', close);

    document.getElementById('crop-apply').addEventListener('click', async () => {
      // Warn before discarding lines on an orientation flip.
      const change = app.cropRect ? cropChange(app.cropRect, rect) : { orientationChanged: false };
      if (change.orientationChanged && app.lines.length > 0 &&
          !(await app.confirm('Changing the crop orientation will remove all placed lines and markers. Continue?', { title: 'Change orientation', danger: true }))) {
        return;
      }
      const hadLines = app.lines.length;
      app.imageModel.applyCrop(rect, { recalc: true });
      close();
      if (change.orientationChanged && hadLines) notify('Cropped — lines removed (orientation changed)', 'ok');
      else notify('Image cropped', 'ok');
    });
  }
}
define('stencil-crop-modal', StencilCropModal);
