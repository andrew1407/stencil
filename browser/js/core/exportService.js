import { notify, shortName, isSplitCompare } from '../utils.js';
import { arriveFrom } from '../ui/motion.js';
import { validateLayout } from './layout.js';
import { serializeProjectFile, parseProjectFile } from './projectFile.js';

// ── ExportService: image/layout export, clipboard, and file IO ──────
// Holds no state of its own: reads the app's editor state and routes every mutation back
// through the app's shared methods, matching the back-reference collaborator pattern.
export class ExportService {
  constructor(app) {
    this.app = app;
  }

  // Trigger a client-side download of `blob` as `filename` via a transient <a> (the
  // object-URL dance shared by downloadJSON and the .stencil save fallback).
  #downloadBlob(blob, filename) {
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

  // File-name suffix + clipboard/toast label per export variant.
  static #VARIANT_META = {
    current:  { suffix: '',          copyLabel: 'Image copied to clipboard' },
    original: { suffix: '-original', copyLabel: 'Original image copied to clipboard' },
    tint:     { suffix: '-tint',     copyLabel: 'Tinted image copied to clipboard' },
    split:    { suffix: '-split',    copyLabel: 'Split image copied to clipboard' },
  };

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
    const suffix = ExportService.#VARIANT_META[variant]?.suffix || '';
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
    this.#downloadBlob(blob, `${app.imageBaseName || 'drawing'}-layout.json`);
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

  // ── Clipboard: copy the current image (with active filter) ──
  // The write() MUST run synchronously inside the Cmd/Ctrl+C gesture with a Promise-valued
  // ClipboardItem — deferring into the async toBlob callback loses the user-activation
  // (NotAllowedError on macOS WebKit). Returns a promise resolving on a successful write
  // and REJECTING on failure, so a plan-driven copy (§10 `copy` op) can report the outcome.
  //   variant: 'current' (Ctrl+C) | 'original' (Ctrl+Shift+C) | 'tint' (Ctrl+Alt+C) |
  //            'split' (Ctrl+C's own slot in a compare view; the call site decides which).
  copyImageToClipboard(variant = 'current') {
    // Rejections are PRE-CAUGHT on a side branch so a fire-and-forget caller (the
    // toolbar button, the chainable facade) never trips unhandledrejection, while an
    // awaiting caller (the §10 copy op) still observes the real outcome.
    const outcome = (() => {
      const app = this.app;
      if (!app.image) { notify('No image to copy', 'fail'); return Promise.reject(new Error('No image to copy')); }
      if (variant === 'split' && !isSplitCompare(app)) {
        notify('Turn on split compare to copy with the splitter', 'fail');
        return Promise.reject(new Error('Split compare is not active'));
      }
      try {
        // 'current' is always the plain edited frame; 'split' is its own explicit variant
        // (see the class-level comment above) — no substitution happens in here.
        const off = this.renderExportCanvas(variant);
        const blobP = new Promise((res, rej) =>
          off.toBlob(b => b ? res(b) : rej(new Error('Image encode failed')), 'image/png'));
        const label = ExportService.#VARIANT_META[variant]?.copyLabel || 'Image copied to clipboard';
        return navigator.clipboard.write([new ClipboardItem({ 'image/png': blobP })])
          .then(() => notify(label, 'ok'))
          .catch(err => { notify('Copy failed: ' + (err.message || err), 'fail'); throw err; });
      } catch (e) {
        notify('Copy failed: ' + e.message, 'fail');
        return Promise.reject(e);
      }
    })();
    outcome.catch(() => { /* observed above; awaiting callers re-observe */ });
    return outcome;
  }

  // ── Clipboard: copy layout JSON text ──
  // Copies the FULL layout — lines plus every applied edit (filter/tint, crop, rotation, page
  // format, formulas) via currentLayoutPayload, so a paste reproduces the whole editor state.
  copyLayoutToClipboard() {
    // Same outcome-promise shape as copyImageToClipboard: rejections are pre-caught
    // on a side branch so fire-and-forget callers (toolbar, chainable facade) never
    // trip unhandledrejection, while the §10 copy op still observes the real outcome.
    const outcome = (() => {
      const app = this.app;
      if (!app.lines || app.lines.length === 0) {
        notify('No layout to copy', 'fail');
        return Promise.reject(new Error('No layout to copy'));
      }
      const txt = JSON.stringify(app.currentLayoutPayload(), null, 2);
      return navigator.clipboard.writeText(txt)
        .then(() => notify('Layout JSON copied', 'ok'))
        .catch(err => { notify('Copy failed: ' + (err.message || err), 'fail'); throw err; });
    })();
    outcome.catch(() => { /* observed above; awaiting callers re-observe */ });
    return outcome;
  }

  // ── .stencil project file: whole-project save/open (image + layout + metadata + optional theme) ──
  // Saves via the File System Access Save-As dialog when supported, else the download-blob fallback.
  async saveProjectFile({ includeTheme = true } = {}) {
    const app = this.app;
    if (!app.image || !app.imageDataUrl) { notify('Open an image first', 'fail'); return; }
    const text = serializeProjectFile(app.projectFileState({ includeTheme }));
    const base = (app.storage.store.getMeta(app.activeProjectId)?.name || app.imageBaseName || 'project')
      .replace(/[/\\?%*:|"<>]/g, '-').trim() || 'project';
    const filename = `${base}.stencil`;
    try {
      if (window.showSaveFilePicker) {
        const handle = await window.showSaveFilePicker({
          suggestedName: filename,
          types: [{ description: 'Stencil project', accept: { 'application/x-stencil': ['.stencil'] } }],
        });
        const writable = await handle.createWritable();
        await writable.write(text);
        await writable.close();
        // Keep the handle so the project can live-sync to this file (auto-save + watch).
        await app.stencilSync.link(handle, handle.name || filename);
      } else {
        this.#downloadBlob(new Blob([text], { type: 'application/x-stencil' }), filename);
      }
      notify('Project saved', 'ok');
    } catch (err) {
      if (err && err.name === 'AbortError') return;   // user cancelled the picker — not an error
      notify('Could not save project: ' + (err.message || err), 'fail');
    }
  }

  // Open a .stencil project from a File (file input / drag-drop) or raw JSON text. Validates,
  // then hands off to DrawingApp.applyProjectFile (which loads it as a fresh local project).
  async openProjectFile(input, { from = null } = {}) {
    const app = this.app;
    let text;
    try { text = typeof input === 'string' ? input : await input.text(); }
    catch { notify('Could not read project file', 'fail'); return; }
    const res = parseProjectFile(text);
    if (!res.ok) { notify('Invalid .stencil file: ' + res.error, 'fail'); return; }
    try {
      const name = await app.applyProjectFile(res.project);
      // Dropped in: the canvas flies out of the drop point (a project opened from the
      // picker has no point and gets the plain landing).
      arriveFrom(document.getElementById('canvas-container'), from);
      notify(`Opened project “${shortName(name)}”`, 'ok');
    } catch (err) {
      notify('Could not open project: ' + (err.message || err), 'fail');
    }
  }

  // Prompt for a .stencil file (FS Access open picker when available, else a transient <input>).
  async pickAndOpenProjectFile() {
    if (window.showOpenFilePicker) {
      try {
        const [handle] = await window.showOpenFilePicker({
          types: [{ description: 'Stencil project', accept: { 'application/x-stencil': ['.stencil'] } }],
          multiple: false,
        });
        const file = await handle.getFile();
        await this.openProjectFile(file);
        // Keep the handle so this project can live-sync to the file it was opened from.
        await this.app.stencilSync.link(handle, file.name);
      } catch (err) {
        if (err && err.name === 'AbortError') return;
        notify('Could not open project: ' + (err.message || err), 'fail');
      }
      return;
    }
    const inp = document.createElement('input');
    inp.type = 'file';
    inp.accept = '.stencil,application/x-stencil';
    inp.onchange = () => { const f = inp.files && inp.files[0]; if (f) this.openProjectFile(f); };
    inp.click();
  }

  // Delete the linked .stencil file from disk (Chromium FileSystemHandle.remove()) after a confirm,
  // then drop the link so live-sync stops. Needs a retained handle, so only a file-linked project
  // (saved/opened via the picker) can — the project stays open; only the on-disk file is removed.
  async deleteProjectFile() {
    const app = this.app;
    const sync = app.stencilSync;
    if (!sync.linked) { notify('No linked .stencil file to delete', 'fail'); return; }
    const handle = sync.handle;
    if (typeof handle.remove !== 'function') {
      notify('Deleting files needs a newer Chromium browser', 'fail');
      return;
    }
    const name = sync.name || 'this project file';
    if (!(await app.confirm(
      `Delete “${name}” from disk? This can’t be undone. The project stays open here.`,
      { title: 'Delete project file', confirmLabel: 'Delete file', confirmIcon: 'trash', cancelLabel: 'Cancel' }))) {
      notify('Delete canceled', 'info');
      return;
    }
    try {
      await handle.remove();
      sync.unlink();               // stop auto-save/watch — there's no file to sync to anymore
      notify(`Deleted “${shortName(name)}”`, 'ok');
    } catch (err) {
      if (err && err.name === 'AbortError') return;   // some impls surface a cancelled perm prompt as AbortError
      notify('Could not delete file: ' + (err.message || err), 'fail');
    }
  }

  // ── Apply a layout object pasted from the clipboard ──
  // `from` is the drop point when the layout arrived by drag-and-drop; the canvas plays
  // in out of it once the lines are installed.
  async applyPastedLayout(data, from = null) {
    await this.#applyValidatedLayout(data, {
      source: 'pasted JSON',
      cancelMsg: 'Layout paste canceled',
      successMsg: 'Layout pasted from clipboard',
      from,
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
  async #applyValidatedLayout(data, { source, cancelMsg, successMsg, from = null }) {
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
    // Existing lines: offer to KEEP them and add the incoming ones on top, rather than
    // forcing an all-or-nothing replace. Cancel still backs out entirely.
    let mode = 'replace';
    if (verdict.needsReplaceConfirm) {
      const choice = await app.askAlt(
        `Add ${source} on top of the current layout, or replace it?`,
        {
          title: 'Existing layout',
          // Glyphs for the two real answers: swap one layout for the other, or stack
          // the incoming lines on the existing ones.
          confirmLabel: 'Replace', confirmIcon: 'swap',
          altLabel: 'Combine', altIcon: 'layers',
        });
      if (!choice) { notify(cancelMsg, 'info'); return; }
      mode = choice === 'alt' ? 'combine' : 'replace';
    }
    if (verdict.needsDimMismatchConfirm && !(await app.confirm('Image dimensions do not match. Continue anyway?', { title: 'Dimension mismatch' }))) {
      notify(cancelMsg, 'info');
      return;
    }
    this.#installLines(mode === 'combine' ? [...(app.lines || []), ...verdict.lines] : verdict.lines);
    // Dropped layouts fly in out of the drop point; a paste (no point) just lands.
    if (from) arriveFrom(document.getElementById('canvas-container'), from);
    notify(mode === 'combine' ? `${successMsg} (combined)` : successMsg, 'ok');
  }

  /**
   * Install a validated line list and refresh everything that reflects it.
   * @param {Array} lines - Validated lines to become `app.lines`.
   * @param {{history?: boolean}} [opts] - `history:false` keeps the change out of undo.
   */
  #installLines(lines, { history = true } = {}) {
    const app = this.app;
    app.lines = lines;
    if (history) app.saveHistory();
    app.renderer.redraw();
    app.updateButtons();
    if (app.lines.length > 0) app.coordTable.update(app.lines[app.lines.length - 1].points);
  }

  /**
   * Install a layout with NO prompt and NO toast — the programmatic path behind
   * `stencil.setLines()`. The replace/dimension confirmations exist to protect a user
   * from a surprise paste; a caller passing lines in code already knows what it is
   * installing. Returns true when the layout was applied.
   * @param {object} data - Layout payload (expects a `lines` array).
   * @param {{history?: boolean}} [opts] - `history:false` keeps it out of undo.
   * @returns {boolean}
   */
  installLayout(data, opts = {}) {
    const app = this.app;
    const verdict = validateLayout(data, {
      hasImage: !!app.image,
      imgW: app.canvas.width,
      imgH: app.canvas.height,
      hasExistingLines: !!(app.lines && app.lines.length > 0),
    });
    if (!verdict.ok) return false;
    const lines = opts.mode === 'combine' ? [...(app.lines || []), ...verdict.lines] : verdict.lines;
    this.#installLines(lines, opts);
    return true;
  }
}
