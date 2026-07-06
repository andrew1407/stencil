import { notify } from '../utils.js';
import { validateLayout } from './layout.js';

// ── ExportService: image/layout export, clipboard, and file IO ──────
// Extracted from drawingApp.js (the export/clipboard/upload cluster). Holds no state
// of its own: it reads the app's editor state and routes every mutation back through
// the app's shared methods (saveHistory / renderer / coordTable / saveToServer), matching
// the Renderer/Storage/CoordTable/ZoomPan back-reference collaborator pattern.
export class ExportService {
  constructor(app) {
    this.app = app;
  }

  // Render the image (with its current filter) plus all visible lines/points onto a
  // fresh full-resolution offscreen canvas. Shared by saveImage / copyImageToClipboard
  // / shareImage so every image action produces the same annotated result. The renderer's
  // draw helpers write to app.ctx; point that at the offscreen ctx for the export, then restore.
  renderExportCanvas() {
    const app = this.app;
    const offscreen = document.createElement('canvas');
    offscreen.width = app.canvas.width;
    offscreen.height = app.canvas.height;
    const ctx = offscreen.getContext('2d');
    const savedCtx = app.ctx;
    app.ctx = ctx;
    app.renderer.drawImageWithFilter(ctx);
    if (app.showLines) {
      app.lines.forEach(line => app.renderer.drawLine(line, false));
    } else if (app.showPoints) {
      app.lines.forEach(line => {
        line.points.forEach(p => app.renderer.drawPoint(p, line.color, line.markerSize ?? app.markerSize, false));
      });
    }
    app.ctx = savedCtx;
    return offscreen;
  }

  saveImage() {
    const app = this.app;
    if (!app.image) {
      notify('No image loaded', 'fail');
      return;
    }
    const offscreen = this.renderExportCanvas();

    // Download — use original image name if available
    const baseName = app.imageBaseName || 'drawing';
    const ext = app.imageExt      || 'png';
    const mimeMap = { jpg: 'image/jpeg', jpeg: 'image/jpeg', webp: 'image/webp', png: 'image/png' };
    const mime = mimeMap[ext] || 'image/png';
    const outExt = (ext === 'jpg' || ext === 'jpeg') ? 'jpg' : (mimeMap[ext] ? ext : 'png');
    const link = document.createElement('a');
    link.download = `${baseName}-drawing.${outExt}`;
    link.href = offscreen.toDataURL(mime);
    link.click();

    // A server-linked session also writes the annotated result + layout back.
    if (app.remoteLink) app.remoteSync.saveToServer();
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
      navigator.share({ files: [file], title: `${baseName} — Stencil` })
        .catch(err => { if (err && err.name !== 'AbortError') notify('Share failed', 'fail'); });
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
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${app.imageBaseName || 'drawing'}-layout.json`;
    a.click();
    URL.revokeObjectURL(url);
  }

  uploadJSON(e) {
    const file = e.target.files[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = async event => {
      try {
        const data = JSON.parse(event.target.result);
        await this.#applyValidatedLayout(data, {
          source: 'uploaded JSON',
          cancelMsg: 'Upload canceled',
          successMsg: 'JSON loaded successfully'
        });
      } catch (err) {
        notify('Error loading JSON: ' + err.message, 'fail');
      }
    };
    reader.readAsText(file);
    e.target.value = '';
  }

  // ── Clipboard: copy current image (with active filter) ──
  // The write() MUST run synchronously inside the Cmd/Ctrl+C user gesture, so the clipboard
  // gets a Promise-valued ClipboardItem and resolves the PNG blob behind it. Deferring write()
  // into the async toBlob callback loses the user-activation → NotAllowedError on macOS WebKit
  // (and intermittently Chrome), so nothing copies. See FEATURE 3 in the change contract.
  copyImageToClipboard() {
    const app = this.app;
    if (!app.image) { notify('No image to copy', 'fail'); return; }
    try {
      const off = this.renderExportCanvas();
      const blobP = new Promise((res, rej) =>
        off.toBlob(b => b ? res(b) : rej(new Error('Image encode failed')), 'image/png'));
      navigator.clipboard.write([new ClipboardItem({ 'image/png': blobP })])
        .then(() => notify('Image copied to clipboard', 'ok'))
        .catch(err => notify('Copy failed: ' + (err.message || err), 'fail'));
    } catch (e) {
      notify('Copy failed: ' + e.message, 'fail');
    }
  }

  // ── Clipboard: copy layout JSON text ──
  // Copies the FULL layout — lines plus every applied edit (filter/tint, crop, rotation, page
  // format, formulas) via currentLayoutPayload, so a paste reproduces the whole editor state.
  copyLayoutToClipboard() {
    const app = this.app;
    if (!app.lines || app.lines.length === 0) {
      notify('No layout to copy', 'fail');
      return;
    }
    const data = app.currentLayoutPayload();
    const txt = JSON.stringify(data, null, 2);
    navigator.clipboard.writeText(txt).then(
      () => notify('Layout JSON copied', 'ok'),
      err => notify('Copy failed: ' + (err.message || err), 'fail')
    );
  }

  // ── Apply a layout object pasted from the clipboard ──
  async applyPastedLayout(data) {
    await this.#applyValidatedLayout(data, {
      source: 'pasted JSON',
      cancelMsg: 'Layout paste canceled',
      successMsg: 'Layout pasted from clipboard'
    });
  }

  /**
   * Validate a layout payload and, after any needed confirmations, install it
   * as the current lines. Shared by JSON file upload and clipboard paste.
   * @param {object} data - Parsed layout payload (expects a `lines` array).
   * @param {{source: string, cancelMsg: string, successMsg: string}} opts -
   *   `source` names the layout's origin in the replace prompt; `cancelMsg` and
   *   `successMsg` are the toasts shown on cancel and success.
   * @returns {Promise<void>}
   */
  async #applyValidatedLayout(data, { source, cancelMsg, successMsg }) {
    const app = this.app;
    const verdict = validateLayout(data, {
      hasImage: !!app.image,
      imgW: app.canvas.width,
      imgH: app.canvas.height,
      hasExistingLines: !!(app.lines && app.lines.length > 0)
    });
    if (!verdict.ok) {
      notify('Load an image first', 'fail');
      return;
    }
    if (verdict.needsReplaceConfirm && !(await app.confirm(`Replace current layout with ${source}?`, { title: 'Replace layout' }))) {
      notify(cancelMsg, 'fail');
      return;
    }
    if (verdict.needsDimMismatchConfirm && !(await app.confirm('Image dimensions do not match. Continue anyway?', { title: 'Dimension mismatch' }))) {
      notify(cancelMsg, 'fail');
      return;
    }
    app.lines = verdict.lines;
    app.saveHistory();
    app.renderer.redraw();
    app.updateButtons();
    if (app.lines.length > 0) app.coordTable.update(app.lines[app.lines.length - 1].points);
    notify(successMsg, 'ok');
  }
}
