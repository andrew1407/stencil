import { StencilElement, hostTag, define, wireModalShell } from '../base.js';
import { notify } from '../../utils.js';
import constants from '../../config/constants.json' with { type: 'json' };
import { cropAspect, centeredCrop, resizeCropFromCorner, moveCropClamped, scaleCropCentered, cropChange, isAlbumOrientation, swapCropOrientation } from '../../core/parse/cropGeometry.js';
import { icon, spinIconOnce } from '../icons.js';
import { tweenRect } from '../motion/rectTween.js';
const { PAGE_SIZES } = constants;

// A rect over the original image, locked to the page aspect; confirm stores it without
// replacing the original. Geometry runs in the shared core (cropGeometry.js → wasm).
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
                    <div id="crop-box" style="position:absolute;box-sizing:border-box;border:2px solid var(--accent-2);cursor:move;display:none;">
                        <span class="crop-handle" data-corner="0" style="position:absolute;width:14px;height:14px;background:var(--accent-2);border:2px solid #fff;border-radius:50%;left:-8px;top:-8px;cursor:nwse-resize;"></span>
                        <span class="crop-handle" data-corner="1" style="position:absolute;width:14px;height:14px;background:var(--accent-2);border:2px solid #fff;border-radius:50%;right:-8px;top:-8px;cursor:nesw-resize;"></span>
                        <span class="crop-handle" data-corner="2" style="position:absolute;width:14px;height:14px;background:var(--accent-2);border:2px solid #fff;border-radius:50%;right:-8px;bottom:-8px;cursor:nwse-resize;"></span>
                        <span class="crop-handle" data-corner="3" style="position:absolute;width:14px;height:14px;background:var(--accent-2);border:2px solid #fff;border-radius:50%;left:-8px;bottom:-8px;cursor:nesw-resize;"></span>
                    </div>
                </div>
                <div id="crop-dims" style="font-size:13px;color:var(--text-muted);"></div>
            </div>
            <div class="settings-footer">
                <!-- Hint first, every button after it: text left, buttons right — and when the
                     footer wraps, the hint takes the top line and ALL the buttons the row below
                     (the same rule the projects footer and the desktop FooterWrap follow). -->
                <span class="footer-hint">Drag to move · drag a corner to resize (aspect locked to the page).</span>
                <button id="crop-orientation" class="btn-icon-text" data-title="Swap album / portrait — flips the crop orientation">${icon('swap', { size: 14 })}<span>Album</span></button>
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
    const shade = document.getElementById('crop-shade');
    const dims = document.getElementById('crop-dims');
    const orientBtn = document.getElementById('crop-orientation');

    let rect = { x: 0, y: 0, width: 0, height: 0 };
    let album = false;
    let aspect = 1;
    let scale = 1;
    let iw = 0, ih = 0;

    // Page natural dimensions (cm), not orientation-swapped; mirrors blankImageModal's pageDims().
    const pageDims = () => (app.pageSize === 'custom'
      ? { width: app.customPageWidth, height: app.customPageHeight }
      : PAGE_SIZES[app.pageSize] || PAGE_SIZES.A4);

    const computeScale = () => {
      const r = img.getBoundingClientRect();
      scale = iw > 0 && r.width > 0 ? r.width / iw : 1;
    };

    // The orientation flip's own rect flight; any plain render settles it (rectTween.js).
    let flight = null;
    const settle = () => { if (flight) { flight(); flight = null; } };
    const paintBox = (r) => {
      box.style.left = (r.x * scale) + 'px';
      box.style.top = (r.y * scale) + 'px';
      box.style.width = (r.width * scale) + 'px';
      box.style.height = (r.height * scale) + 'px';
      shade.style.left = box.style.left;
      shade.style.top = box.style.top;
      shade.style.width = box.style.width;
      shade.style.height = box.style.height;
    };
    // Only when the face actually changes: renderBox runs per drag frame, and rewriting the
    // glyph there rebuilds the SVG sixty times a second and drops any turn spinIconOnce started.
    let shownAlbum = null;
    const renderOrientFace = () => {
      if (shownAlbum === album) return;
      shownAlbum = album;
      orientBtn.innerHTML = icon('swap', { size: 14 }) + `<span>${album ? 'Album' : 'Portrait'}</span>`;
    };
    const renderBox = () => {
      settle();
      box.style.display = 'block';
      shade.style.display = 'block';
      paintBox(rect);
      dims.textContent = `${Math.round(rect.width)} × ${Math.round(rect.height)} px · ${album ? 'Album (landscape)' : 'Portrait'}`;
      renderOrientFace();
    };

    // swapCropOrientation carries the drag across the flip instead of resetting it (centeredCrop
    // with no rect yet), and the box eases there (desktop twin: CropPreview::setAlbum).
    const recenter = () => {
      const from = { ...rect };
      aspect = cropAspect(pageDims().width, pageDims().height, album);
      rect = swapCropOrientation(rect, aspect, iw, ih);
      renderBox();
      if (from.width >= 1) flight = tweenRect(from, rect, paintBox);
    };

    const paint = () => { computeScale(); renderBox(); };

    // Seeding hangs off the shell, not the click: the window is also opened without one
    // (stencil.openCropWindow), and a src that is already decoded fires no load event.
    const seedPreview = () => {
      if (!app.originalImage || !app.imageDataUrl) return;
      // The rotated original, so the crop rect (in rotated pixel space) lines up.
      const orig = app.imageModel.effectiveOriginalDims();
      iw = orig.width;
      ih = orig.height;
      rect = app.cropRect ? { ...app.cropRect } : centeredCrop(iw, ih, cropAspect(pageDims().width, pageDims().height, isAlbumOrientation(iw, ih)));
      album = isAlbumOrientation(rect.width, rect.height);
      aspect = cropAspect(pageDims().width, pageDims().height, album);
      img.onload = paint;
      img.src = app.imageModel.effectiveOriginalDataUrl();
      // A decoded src paints once the box has a laid-out size — display:none measures 0.
      if (img.complete && img.naturalWidth) requestAnimationFrame(paint);
    };

    const { open, close } = wireModalShell(overlay, null, document.getElementById('crop-close'), {
      onOpen: seedPreview,
      onClose: () => { settle(); box.style.display = 'none'; }
    });

    const openCrop = () => {
      if (!app.originalImage || !app.imageDataUrl) {
        notify('Open an image first', 'fail');
        return;
      }
      open();
      if (img.complete && img.naturalWidth) paint();
    };
    document.getElementById('crop-image').addEventListener('click', openCrop);

    const toImage = (clientX, clientY) => {
      const r = img.getBoundingClientRect();
      return { x: (clientX - r.left) / scale, y: (clientY - r.top) / scale };
    };

    let drag = null;
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

    // Wheel / trackpad pinch (a ctrl+wheel event in Chromium) over the crop rect scales it
    // from its centre; passive:false to preventDefault.
    document.getElementById('crop-stage').addEventListener('wheel', e => {
      if (box.style.display === 'none' || iw <= 0) return;
      const b = box.getBoundingClientRect();
      if (e.clientX < b.left || e.clientX > b.right || e.clientY < b.top || e.clientY > b.bottom) return;
      e.preventDefault();
      const factor = Math.pow(1.0015, -e.deltaY);
      rect = scaleCropCentered(rect, factor, aspect, iw, ih);
      // Re-anchor a live drag to the new size so the next mousemove does not snap it back.
      if (drag) { drag.startRect = { ...rect }; drag.startImg = toImage(e.clientX, e.clientY); }
      renderBox();
    }, { passive: false });

    // spun AFTER recenter(): that repaints the glyph, which would drop a running turn.
    orientBtn.addEventListener('click', () => { album = !album; recenter(); spinIconOnce(orientBtn); });
    document.getElementById('crop-cancel').addEventListener('click', close);

    document.getElementById('crop-apply').addEventListener('click', async () => {
      const change = app.cropRect ? cropChange(app.cropRect, rect) : { orientationChanged: false };
      if (change.orientationChanged && app.lines.length > 0 &&
          !(await app.confirm('Changing the crop orientation will remove all placed lines and points. Continue?', { title: 'Change orientation', danger: true, confirmIcon: 'crop' }))) {
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
