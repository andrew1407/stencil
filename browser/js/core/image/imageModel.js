import { notify } from '../../utils.js';
import constants from '../../config/constants.json' with { type: 'json' };
import { cropAspect, centeredCrop, cropChange, isAlbumOrientation, scaleLinePoints, rotateCropRectQuarter, rotateLinePointsQuarter } from '../cropGeometry.js';

const { PAGE_SIZES } = constants;

// Non-destructive crop + quarter-turn rotation over `originalImage` (never modified);
// `cropRect` is in rotated-original pixels. Geometry: cropGeometry.js (wasm/JS twin).
export class ImageModel {
  constructor(app) {
    this.app = app;
  }

// Only the aspect is used by cropping. Mirrors blankImageModal.pageDims.
  #pageCmDims() {
    const app = this.app;
    return app.pageSize === 'custom'
      ? { width: app.customPageWidth, height: app.customPageHeight }
      : (PAGE_SIZES[app.pageSize] || PAGE_SIZES.A4);
  }

// Page aspect in the orientation matching the image; `albumOverride` forces album (true) /
// portrait (false). Public so storage can default-crop legacy projects.
  defaultCropRect(albumOverride) {
    const { width: iw, height: ih } = this.rotatedOriginalDims();
    const isAlbum = (albumOverride == null) ? isAlbumOrientation(iw, ih) : !!albumOverride;
    const dims = this.#pageCmDims();
    const aspect = cropAspect(dims.width, dims.height, isAlbum);
    return this.roundRect(centeredCrop(iw, ih, aspect), iw, ih);
  }

// The pixel space `cropRect` lives in: odd quarter-turns swap width and height.
  rotatedOriginalDims() {
    const img = this.app.originalImage;
    const w = img.width, h = img.height;
    return (this.app.rotationQuarters % 2) ? { width: h, height: w } : { width: w, height: h };
  }

// The untouched bitmap for no rotation, otherwise a freshly-rotated canvas.
  #rotatedOriginalCanvas() {
    const img = this.app.originalImage;
    const q = ((this.app.rotationQuarters % 4) + 4) % 4;
    if (q === 0) return img;
    const swap = q % 2 === 1;
    const c = document.createElement('canvas');
    c.width = swap ? img.height : img.width;
    c.height = swap ? img.width : img.height;
    const ctx = c.getContext('2d');
    ctx.translate(c.width / 2, c.height / 2);
    ctx.rotate(q * Math.PI / 2);
    ctx.drawImage(img, -img.width / 2, -img.height / 2);
    return c;
  }

// For the crop modal; the stored data URL is returned untouched when not rotated (no re-encode).
  effectiveOriginalDims() { return this.rotatedOriginalDims(); }
  effectiveOriginalDataUrl() {
    if (!this.app.rotationQuarters) return this.app.imageDataUrl;
    return this.#rotatedOriginalCanvas().toDataURL();
  }

// Integer pixels, clamped inside the rotated original.
  roundRect(r, iw = this.rotatedOriginalDims().width, ih = this.rotatedOriginalDims().height) {
// Canonical {w,h} wins over legacy {width,height}.
    const w = Math.max(1, Math.min(Math.round(r.w ?? r.width), iw));
    const h = Math.max(1, Math.min(Math.round(r.h ?? r.height), ih));
    const x = Math.max(0, Math.min(Math.round(r.x), iw - w));
    const y = Math.max(0, Math.min(Math.round(r.y), ih - h));
    return { x, y, width: w, height: h };
  }

// Public so storage can rebuild the view after restoring original + rotation + cropRect.
  rebuildCroppedImage() {
    const app = this.app;
    const src = this.#rotatedOriginalCanvas();
    const r = app.cropRect;
    const c = document.createElement('canvas');
    c.width = r.width;
    c.height = r.height;
    c.getContext('2d').drawImage(src, r.x, r.y, r.width, r.height, 0, 0, r.width, r.height);
    app.image = c;
    app.canvas.width = r.width;
    app.canvas.height = r.height;
  }

// After rotate or crop: clear the selection, reset history to the current lines, refit, persist.
  #afterImageGeometryChange() {
    const app = this.app;
    app.currentLine = null;
    app.selectedLineIdx = -1;
    app.coordLineIdx = -1;
    app.focusedPtIdx = -1;
    app.hideSelectionPanels();
    app.history.reset(app.lines);
    app.zoomPan.fitToWindow();
    app.updateInfo();
    app.renderer.redraw();
    app.updateButtons();
    app.updateCoordStatus();
    app.coordTable.update(app.lines.length > 0 ? app.lines[app.lines.length - 1].points : null);
    app.storage.save();
    app.remoteSync.scheduleRemoteSync();
  }

// dir < 0 rotates left (CCW), dir > 0 right (CW); the crop window and every line follow.
  rotateImage(dir) {
    const app = this.app;
    if (!app.originalImage) {
      notify('Open an image first', 'fail');
      return;
    }
    const clockwise = dir > 0;
    const dims = this.rotatedOriginalDims();
// Points rotate inside the OLD crop box.
    rotateLinePointsQuarter(app.lines, app.cropRect.width, app.cropRect.height, clockwise);
    const rotated = rotateCropRectQuarter(app.cropRect, dims.width, dims.height, clockwise);
    app.rotationQuarters = (((app.rotationQuarters + (clockwise ? 1 : -1)) % 4) + 4) % 4;
    app.cropRect = this.roundRect(rotated);
    this.rebuildCroppedImage();
    this.#afterImageGeometryChange();
  }

// With opts.recalc, lines are cleared on an orientation flip or rescaled to the new size.
  applyCrop(rect, opts = {}) {
    const app = this.app;
    if (!app.originalImage) return;
    const newRect = this.roundRect(rect);
    if (opts.recalc && app.cropRect) {
      const change = cropChange(app.cropRect, newRect);
      if (change.orientationChanged) app.lines = [];
      else if (change.scale !== 1) scaleLinePoints(app.lines, change.scale);
    }
    app.cropRect = newRect;
    this.rebuildCroppedImage();
    this.#afterImageGeometryChange();
  }
}
