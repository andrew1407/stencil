import { notify } from '../utils.js';
import constants from '../config/constants.json' with { type: 'json' };
import { cropAspect, centeredCrop, cropChange, isAlbumOrientation, scaleLinePoints, rotateCropRectQuarter, rotateLinePointsQuarter } from './cropGeometry.js';

const { PAGE_SIZES } = constants;

// ── ImageModel: non-destructive crop + quarter-turn rotation ────────
// Extracted from drawingApp.js. Owns the geometry transforms over the app's `originalImage`
// (never modified) → the working cropped `image`, tracked by `cropRect` (in rotated-original
// pixels) and `rotationQuarters`. Holds no state itself: it reads/writes those fields on the
// back-referenced app, matching the Renderer/Storage collaborator pattern. Pure geometry math
// lives in cropGeometry.js (the wasm/JS-parity twin); this class only orchestrates it + canvas.
export class ImageModel {
  constructor(app) {
    this.app = app;
  }

  // The page's cm dimensions for the active format (custom uses the entered w/h). Only the
  // aspect ratio is used by cropping. Mirrors blankImageModal.pageDims.
  #pageCmDims() {
    const app = this.app;
    return app.pageSize === 'custom'
      ? { width: app.customPageWidth, height: app.customPageHeight }
      : (PAGE_SIZES[app.pageSize] || PAGE_SIZES.A4);
  }

  // The default centered crop for the loaded original: page aspect in the orientation matching
  // the image (album when wider than tall). Public so the storage layer can default-crop legacy
  // projects saved before cropping existed. `albumOverride` forces album (true)/portrait (false);
  // omitted, orientation auto-matches the image (wider-than-tall ⇒ album).
  defaultCropRect(albumOverride) {
    const { w: iw, h: ih } = this.rotatedOriginalDims();
    const isAlbum = (albumOverride == null) ? isAlbumOrientation(iw, ih) : !!albumOverride;
    const dims = this.#pageCmDims();
    const aspect = cropAspect(dims.width, dims.height, isAlbum);
    return this.roundRect(centeredCrop(iw, ih, aspect), iw, ih);
  }

  // Dimensions of the original image after the current rotation is applied (the pixel space
  // `cropRect` lives in). Odd quarter-turns swap width and height. Public — loadImageFromFile
  // reads it too.
  rotatedOriginalDims() {
    const img = this.app.originalImage;
    const w = img.width, h = img.height;
    return (this.app.rotationQuarters % 2) ? { w: h, h: w } : { w, h };
  }

  // The original image rotated by the current quarter-turn count (clockwise). For no rotation
  // the untouched bitmap is returned; otherwise a freshly-rotated canvas. Used by
  // rebuildCroppedImage and the crop modal's preview.
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

  // The rotated original as { w, h } + a data URL, for the crop modal (which previews the full
  // original). Returns the stored data URL untouched when not rotated, avoiding a re-encode.
  effectiveOriginalDims() { return this.rotatedOriginalDims(); }
  effectiveOriginalDataUrl() {
    if (!this.app.rotationQuarters) return this.app.imageDataUrl;
    return this.#rotatedOriginalCanvas().toDataURL();
  }

  // Snap a crop rect to integer pixels, clamped inside the rotated original image. Public —
  // loadImageFromFile clamps caller-supplied crops with it.
  roundRect(r, iw = this.rotatedOriginalDims().w, ih = this.rotatedOriginalDims().h) {
    const w = Math.max(1, Math.min(Math.round(r.width), iw));
    const h = Math.max(1, Math.min(Math.round(r.height), ih));
    const x = Math.max(0, Math.min(Math.round(r.x), iw - w));
    const y = Math.max(0, Math.min(Math.round(r.y), ih - h));
    return { x, y, width: w, height: h };
  }

  // Rebuild the working `image` canvas from the rotated `originalImage` + `cropRect`, sizing the
  // main canvas to the crop. Original never modified. Public so storage can rebuild the view
  // after restoring original + rotation + cropRect.
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

  // Reset selection/drawing state and refresh every view after the image geometry changes
  // (rotate or crop): clears the active selection, resets history to the current lines, refits
  // the viewport, and persists.
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
    app.scheduleRemoteSync(); // crop/rotate change the layout's geometry — push it to peers too
  }

  // Rotate the whole image a quarter turn — dir < 0 rotates left (CCW), dir > 0 rotates right
  // (CW). The crop window and every line follow the picture so the framing and the drawing stay
  // put relative to the image content.
  rotateImage(dir) {
    const app = this.app;
    if (!app.originalImage) {
      notify('Open an image first', 'fail');
      return;
    }
    const clockwise = dir > 0;
    const dims = this.rotatedOriginalDims();  // space the crop currently lives in
    // Points first — they rotate inside the OLD crop box (width x height).
    rotateLinePointsQuarter(app.lines, app.cropRect.width, app.cropRect.height, clockwise);
    const rotated = rotateCropRectQuarter(app.cropRect, dims.w, dims.h, clockwise);
    app.rotationQuarters = (((app.rotationQuarters + (clockwise ? 1 : -1)) % 4) + 4) % 4;
    app.cropRect = this.roundRect(rotated);
    this.rebuildCroppedImage();
    this.#afterImageGeometryChange();
  }

  // Apply a new crop rectangle (image-space). With opts.recalc, existing lines are cleared on an
  // orientation flip or rescaled to the new size (the page relation is preserved). Does NOT
  // replace the stored original image.
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
