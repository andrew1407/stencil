import { notify, isSplitCompare } from '../utils.js';
import { VARIANT_META } from './imageVariants.js';
import { copyImageToClipboard, copyLayoutToClipboard } from './clipboardExport.js';
import {
  saveProjectFile, openProjectFile, pickAndOpenProjectFile, deleteProjectFile,
} from './projectFilePicker.js';
import { uploadJSON, applyPastedLayout, installLayout } from './layoutInstall.js';

// Image/layout export, clipboard and file IO. Holds no state: reads the app's editor state
// and routes every mutation back through the app's shared methods.
export class ExportService {
  constructor(app) {
    this.app = app;
  }

  downloadBlob(blob, filename) {
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.click();
    URL.revokeObjectURL(url);
  }

// The renderer writes to app.ctx, so point that at the full-resolution offscreen ctx and
// restore it after.
  renderExportCanvas(variant = 'current') {
    const app = this.app;
    const offscreen = document.createElement('canvas');
    offscreen.width = app.canvas.width;
    offscreen.height = app.canvas.height;
    const ctx = offscreen.getContext('2d');
    const savedCtx = app.ctx;
    app.ctx = ctx;
// An export is the RESTING picture: a vertex still flying must not be baked in mid-air.
    app.strokeFx.suspend();
    if (variant === 'original') {
      ctx.filter = 'none';
      ctx.drawImage(app.image, 0, 0);
    } else {
      app.renderer.drawImageWithFilter(ctx);
      if (variant === 'current' || variant === 'split') {
        if (app.showLines) {
          app.lines.forEach(line => app.renderer.drawLine(line, false));
        } else if (app.showPoints) {
          app.lines.forEach(line => {
            line.points.forEach(p => app.renderer.drawPoint(p, line.color, line.pointSize ?? app.pointSize, false));
          });
        }
      }
      if (variant === 'split') {
        const mode = app.compareMode === 'horizontal' ? 'horizontal' : 'vertical';
        app.renderer.drawCompareSplit(mode, { withDivider: false });
      }
    }
    app.ctx = savedCtx;
    app.strokeFx.resume();
    return offscreen;
  }


  saveImage(variant = 'current') {
    const app = this.app;
    if (!app.image) {
      notify('No image loaded', 'fail');
      return;
    }
    if (variant === 'split' && !isSplitCompare(app)) {
      notify('Turn on split compare to download with the splitter', 'fail');
      return;
    }
// 'split' is its own explicit variant (desktop parity: DataExportController.cpp
// saveImageFile); the call site decides which to ask for, this never substitutes.
    const offscreen = this.renderExportCanvas(variant);

    const baseName = app.imageBaseName || 'drawing';
    const ext = app.imageExt      || 'png';
    const mimeMap = { jpg: 'image/jpeg', jpeg: 'image/jpeg', webp: 'image/webp', png: 'image/png' };
    const mime = mimeMap[ext] || 'image/png';
    const outExt = (ext === 'jpg' || ext === 'jpeg') ? 'jpg' : (mimeMap[ext] ? ext : 'png');
    const suffix = VARIANT_META[variant]?.suffix || '';
    const link = document.createElement('a');
    link.download = `${baseName}-drawing${suffix}.${outExt}`;
    link.href = offscreen.toDataURL(mime);
    link.click();

// Only the canonical "current" download writes back to a linked server.
    if (app.remoteLink && variant === 'current') app.remoteSync.saveToServer();
  }

// Web Share API. Entry points show only when supportsShareFiles() is true; still guarded.
  shareImage() {
    const app = this.app;
    if (!app.image) { notify('No image loaded', 'fail'); return; }
    const off = this.renderExportCanvas();
    const baseName = app.imageBaseName || 'drawing';
    off.toBlob(blob => {
      if (!blob) { notify('Image encode failed', 'fail'); return; }
      const file = new File([blob], `${baseName}-drawing.png`, { type: 'image/png' });
      if (!(navigator.canShare && navigator.canShare({ files: [file] }))) {
        notify('Sharing not supported on this browser', 'fail');
        return;
      }
      try {
// Some engines throw synchronously on a stale user gesture rather than rejecting.
        navigator.share({ files: [file], title: `${baseName} — Stencil` })
          .catch(err => { if (err && err.name !== 'AbortError') notify('Share failed', 'fail'); });
      } catch {
        notify('Share failed', 'fail');
      }
    }, 'image/png');
  }

  downloadJSON() {
    const app = this.app;
    if (app.lines.length === 0) {
      notify('No lines to export', 'fail');
      return;
    }

// The FULL layout, matching the clipboard copy and the server payload.
    const data = app.currentLayoutPayload();

    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    this.downloadBlob(blob, `${app.imageBaseName || 'drawing'}-layout.json`);
  }


  uploadJSON(e) { uploadJSON(this.app, e); }

  copyImageToClipboard(variant = 'current') { return copyImageToClipboard(this, variant); }

  copyLayoutToClipboard() { return copyLayoutToClipboard(this); }

  saveProjectFile(opts = {}) { return saveProjectFile(this, opts); }

  openProjectFile(input, opts = {}) { return openProjectFile(this, input, opts); }

  pickAndOpenProjectFile() { return pickAndOpenProjectFile(this); }

  deleteProjectFile() { return deleteProjectFile(this); }

  applyPastedLayout(data, from = null) { return applyPastedLayout(this.app, data, from); }

  installLayout(data, opts = {}) { return installLayout(this.app, data, opts); }
}
