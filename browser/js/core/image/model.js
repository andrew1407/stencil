import { notify } from '../../utils.js';
import { editorMemento } from '../historyStack.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };
import { cropAspect, centeredCrop, cropChange, isAlbumOrientation, scaleLinePoints, snapCropRect, rotateEditQuarter } from '../parse/cropGeometry.js';

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

// Integer pixels, clamped inside the rotated original; canonical {w,h} wins over {width,height}.
  roundRect(r, iw = this.rotatedOriginalDims().width, ih = this.rotatedOriginalDims().height) {
    return snapCropRect({ x: r.x, y: r.y, width: r.w ?? r.width, height: r.h ?? r.height }, iw, ih);
  }

// Public so storage can rebuild the view after restoring original + rotation + cropRect. The turn
// and the crop offset are one transform, so no full-size rotated copy is made (the map stays integral).
  rebuildCroppedImage() {
    const app = this.app;
    const img = app.originalImage;
    const r = app.cropRect;
    const q = ((app.rotationQuarters % 4) + 4) % 4;
    const c = document.createElement('canvas');
    c.width = r.width;
    c.height = r.height;
    const ctx = c.getContext('2d');
    if (q === 0) ctx.drawImage(img, r.x, r.y, r.width, r.height, 0, 0, r.width, r.height);
    else {
      const { width: rw, height: rh } = this.rotatedOriginalDims();
      ctx.translate(rw / 2 - r.x, rh / 2 - r.y);
      ctx.rotate(q * Math.PI / 2);
      ctx.drawImage(img, -img.width / 2, -img.height / 2);
    }
    app.image = c;
    app.canvas.width = r.width;
    app.canvas.height = r.height;
  }

// An undo step's crop and turn, when they differ from the ones on screen; false when they match
// (or the step carries none). The rebuild is the one a crop runs: no decode.
  restoreView(m) {
    const app = this.app;
    const r = m.cropRect, c = app.cropRect;
    if (!app.originalImage || !r) return false;
    const q = m.rotationQuarters ?? 0;
    if (q === app.rotationQuarters && c && r.x === c.x && r.y === c.y && r.width === c.width && r.height === c.height) return false;
    app.rotationQuarters = q;
    app.cropRect = { x: r.x, y: r.y, width: r.width, height: r.height };
    this.rebuildCroppedImage();
    return true;
  }

// After rotate or crop: one undo step, then the view settles.
  #afterImageGeometryChange() {
    this.app.history.push(editorMemento(this.app));
    this.settleView();
  }

// The picture under the lines changed: clear the selection, refit, persist; `sync: false` for a
// view a peer's layout brought, which the server already holds.
  settleView({ sync = true } = {}) {
    const app = this.app;
    app.currentLine = null;
    app.selectedLineIdx = -1;
    app.coordLineIdx = -1;
    app.focusedPtIdx = -1;
    app.hideSelectionPanels();
    app.zoomPan.fitToWindow();
    app.updateInfo();
    app.renderer.redraw();
    app.updateButtons();
    app.updateCoordStatus();
    app.coordTable.update(app.lines.length > 0 ? app.lines[app.lines.length - 1].points : null);
    app.storage.saveSoon();
    if (sync) app.remoteSync.scheduleRemoteSync();
  }

// dir < 0 rotates left (CCW), dir > 0 right (CW); the crop window and every line follow.
  rotateImage(dir) {
    const app = this.app;
    if (!app.originalImage) {
      notify('Open an image first', 'fail');
      return;
    }
    const img = app.originalImage;
    const turn = rotateEditQuarter(app.lines, app.cropRect, app.rotationQuarters, img.width, img.height, dir > 0);
    app.rotationQuarters = turn.quarters;
    app.cropRect = turn.crop;
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
