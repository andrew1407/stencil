// ── window.stencil's export and project-file actions ────────────────────────
// Download/copy/share, the layout accessors, and the .stencil file round trip —
// every one routed through app.export so console and toolbar share the path.
// A layout argument may be an OBJECT or a raw JSON string — parse the latter so callers can
// hand over clipboard text directly. A non-object (or bad JSON) throws rather than no-op.
const toLayoutObject = (data) => {
  if (typeof data !== 'string') return data;
  let parsed;
  try { parsed = JSON.parse(data); } catch { throw new TypeError('stencil: layout JSON could not be parsed'); }
  if (parsed == null || typeof parsed !== 'object') throw new TypeError('stencil: layout JSON must describe an object');
  return parsed;
};

export const createExportActions = ({ app }) => {
  let stencil;   // the frozen facade, handed over by setFacade after the guard

  const api = {
    // variant: 'current' (default — tint + lines/points) | 'original' | 'tint' (tint only)
    // | 'split' (download only; needs a split compare view, divider baked in).
    downloadImage(variant = 'current') { app.export.saveImage(variant); return stencil; },
    copyLayout() { app.export.copyLayoutToClipboard(); return stencil; },
    // variant: 'current' | 'original' | 'tint' — see downloadImage. 'current' during a
    // split compare view copies the split composite shown on screen (no divider).
    copyImage(variant = 'current') { app.export.copyImageToClipboard(variant); return stencil; }, // alias of copyImageToClipboard
    copyImageToClipboard(variant = 'current') { app.export.copyImageToClipboard(variant); return stencil; },
    shareImage() { app.export.shareImage(); return stencil; },          // Web Share API (mobile/PWA)
    openIn() { document.getElementById('open-in-btn')?.click(); return stencil; },   // Open-in-another-app modal
    downloadLayout() { app.export.downloadJSON(); return stencil; },
    get layout() { return stencil.current?.layout; },
    // Accepts a layout OBJECT or a raw JSON string. Routes through the clipboard-paste
    // path, so an existing layout raises the Combine / Replace / Cancel prompt.
    set layout(data) { app.export.applyPastedLayout(toLayoutObject(data)); },
    // Apply a layout with no prompt or toast. `data` is an object or JSON string;
    // `mode:'combine'` adds on top of the current lines; `history:false` skips undo.
    //   stencil.applyLayout('{"lines":[…]}', { mode: 'combine' })
    applyLayout(data, opts = {}) {
      app.export.installLayout(toLayoutObject(data), opts);
      return stencil;
    },
    // Install lines directly — unlike `stencil.layout = …` (the paste path) this raises
    // no "Replace layout?" prompt and no toast. `history:false` keeps it out of undo.
    //   stencil.setLines([{ points: [{x:0,y:0},{x:10,y:10}], color: '#f00' }])
    setLines(lines, opts = {}) {
      const size = stencil.imageSize;
      const list = Array.isArray(lines) ? lines : [];
      app.export.installLayout(
        size ? { imageWidth: size.width, imageHeight: size.height, lines: list } : { lines: list },
        opts);
      return stencil;
    },

    // Save the whole project as a portable .stencil file (image + layout + metadata + optional
    // theme; `opts.includeTheme` default true embeds light/dark + accent). Resolves to the facade.
    saveProjectFile(opts = {}) { return app.export.saveProjectFile(opts).then(() => stencil); },
    // Open a .stencil project. Pass a File or the raw JSON text; omit to show a file picker.
    // Loads it as a fresh local project. Resolves to the facade.
    openProjectFile(fileOrText) {
      const p = fileOrText == null ? app.export.pickAndOpenProjectFile() : app.export.openProjectFile(fileOrText);
      return p.then(() => stencil);
    },
    // Live two-way sync of a file-linked project to its .stencil (Chromium only): toggle `liveSync`,
    // read `linkedFile` (name or null), `syncNow()` flushes a pending auto-save.
    get liveSync() { return app.stencilSync.supported && app.stencilSync.liveSync; },
    set liveSync(on) { app.stencilSync.liveSync = !!on; },
    get linkedFile() { return app.stencilSync.linked ? app.stencilSync.name : null; },
    syncNow() { return app.stencilSync.flush().then(() => stencil); },
    // Delete the linked .stencil file from disk (Chromium only); confirms first, then unlinks so
    // live-sync stops. The project stays open in the editor. Resolves to the facade.
    deleteProjectFile() { return app.export.deleteProjectFile().then(() => stencil); },
  };

  return { api, setFacade: (f) => { stencil = f; } };
};
