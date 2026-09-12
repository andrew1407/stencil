import { notify, isSplitCompare } from '../utils.js';
import { VARIANT_META } from './imageVariants.js';
import { copyImageToClipboard, copyLayoutToClipboard } from './clipboardExport.js';
import {
  saveProjectFile, openProjectFile, pickAndOpenProjectFile, deleteProjectFile,
} from './projectFilePicker.js';
import { uploadJSON, applyPastedLayout, installLayout } from './layoutInstall.js';

// ── ExportService: image/layout export, clipboard, and file IO ──────
// Holds no state of its own: reads the app's editor state and routes every mutation back
// through the app's shared methods, matching the back-reference collaborator pattern.
export class ExportService {
  constructor(app) {
    this.app = app;
  }

  // Trigger a client-side download of `blob` as `filename` via a transient <a> (the
  // object-URL dance shared by downloadJSON and the .stencil save fallback).
  downloadBlob(blob, filename) {
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.click();
    URL.revokeObjectURL(url);
  }

  // Render one export variant onto a full-resolution offscreen canvas — shared by
  // saveImage / copyImageToClipboard / shareImage / the Alt-hover preview. The renderer's
  // helpers write to app.ctx; point that at the offscreen ctx, then restore.
  //   'current'  — active filter/tint + visible lines/points (the original, default behavior)
  //   'original' — the cropped+rotated original alone: no filter, no annotations
  //   'tint'     — active filter/tint, but no lines/points
  //   'split'    — 'current' plus the compare composite, always CLEAN: the divider and its
  //                knob are editor UI, so exports opt out of drawCompareSplit's withDivider.
  renderExportCanvas(variant = 'current') {
    const app = this.app;
    const offscreen = document.createElement('canvas');
    offscreen.width = app.canvas.width;
    offscreen.height = app.canvas.height;
    const ctx = offscreen.getContext('2d');
    const savedCtx = app.ctx;
    app.ctx = ctx;
    // An export is the RESTING picture: a vertex still flying would otherwise be baked
    // in mid-air, with its spark, by a Ctrl+C landing during the animation.
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

  // Variant suffixes and clipboard labels are data: core/imageVariants.js.

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
    // 'current' is always the plain edited frame (tint + lines/points) — 'split' is its
    // own explicit variant, a separate row/hotkey slot while a split compare view is
    // active (desktop parity: dataExportController.cpp saveImageFile). The PRIMARY
    // gesture (the toolbar click / the saveImage hotkey) decides which of the two to ask
    // for at the call site (controlsBinder.js / exportOptionsMenu.js openFull) — this
    // method itself never substitutes one for the other.
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

    // A server-linked session also writes the annotated result + layout back — only for
    // the canonical "current" download; the other variants are alternate exports, not the
    // project's saved state.
    if (app.remoteLink && variant === 'current') app.remoteSync.saveToServer();
  }

  // Share the annotated image via the Web Share API (mobile/PWA). The Share entry
  // points are only shown when supportsShareFiles() is true (see toolbar/contextMenu
  // wiring), so this is reached only where file sharing works; we still guard defensively.
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
        // Some engines throw synchronously (a stale/expired user gesture) rather than
        // rejecting the promise — caught here so the failure surfaces instead of vanishing
        // silently behind the menu, which had already closed by the time this callback runs.
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

    // Export the FULL layout (lines + filter/crop/rotation/page/formulas), matching the
    // clipboard copy and the server payload so a download round-trips every applied edit.
    const data = app.currentLayoutPayload();

    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    this.downloadBlob(blob, `${app.imageBaseName || 'drawing'}-layout.json`);
  }


  // The clipboard, the .stencil file pickers and the layout-install flow are their own
  // modules beside this one; these keep the one call shape every caller already uses.
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
