import { settleLoadedImage } from './imageSettle.js';

// ── The image-load flow ─────────────────────────────────────────
// DrawingApp.loadImageFromFile's body: the session bookkeeping a load does up front
// (project promotion, filename, provenance, server linkage), then the decode. What the
// decoded image settles is imageSettle.js. Reached as app.loadImageFromFile(file, opts).

export const loadImageFromFile = (app, file, opts = {}) => {
  const replaceInPlace = !!opts.replaceInPlace;
  // A fresh (non-in-place) load starts a DIFFERENT project — drop any .stencil file link the
  // previous project held, or live-sync auto-save would keep writing this new project into the
  // old project's file and overwrite it. A .stencil open re-links right after (see
  // ExportService.pickAndOpenProjectFile); an in-place replace keeps the same project + link.
  if (!replaceInPlace) app.stencilSync?.unlink();
  // Replacing an existing project's image in place: capture what must survive the swap —
  // the annotations to keep, and the OLD image's pin identity to clear — before they're overwritten.
  const keptLines = (replaceInPlace && opts.keepAnnotations) ? app.lines : null;
  const oldImageSource = app.imageSource;
  const oldImageResource = app.imageResource;
  // A different server project becomes its own local project: flush the active one and
  // reset to blank so the promote below makes a distinct record (skip same-project reload / incognito).
  const switchingRemote = !!opts.remoteId
    && (!app.remoteLink || app.remoteLink.remoteId !== opts.remoteId);
  if (switchingRemote && !app.storage.temporary && app.activeProjectId != null
      && !app.storage.incognito) {
    app.storage.save();
    app.storage.newTemporary();
    app.activeProjectId = null;
  }
  // A temporary editor receiving its first image becomes a real project (the final
  // storage.save() persists it; other tabs see it). Exception: incognito editors stay
  // unsaved — do NOT promote, image/lines live in memory only.
  if (!replaceInPlace && (app.storage.temporary || app.activeProjectId == null) && !app.storage.incognito) {
    app.storage.promoteTemporaryToProject();
    app.tabs.reportActive(app.activeProjectId);
  }

  // Store base name and extension for use in download filenames
  const dotIdx = file.name.lastIndexOf('.');
  if (dotIdx !== -1) {
    app.imageBaseName = file.name.slice(0, dotIdx);
    app.imageExt = file.name.slice(dotIdx + 1).toLowerCase();
  } else {
    app.imageBaseName = file.name;
    app.imageExt = 'png';
  }

  // An explicit project name (extension copy-numbering) overrides the filename-
  // derived base used to auto-name the project on first save.
  if (opts.name) app.imageBaseName = opts.name;

  // Provenance comes from the caller (add-by-URL / extension); a plain local
  // upload passes neither, which clears any provenance carried by a prior image.
  app.imageSource = opts.source || null;
  app.imageResource = opts.resource || null;

  // Server linkage. opts.remoteId → reopen an EXISTING server project (link the
  // session, restore its layout below). opts.address WITHOUT a remoteId → CREATE
  // this freshly-loaded image on that server after it loads. Neither → local only.
  // Keep the existing link when replacing in place; otherwise (re)derive it from opts.
  if (!replaceInPlace) {
    app.remoteLink = (opts.address && opts.remoteId)
      ? { address: opts.address, remoteId: opts.remoteId, version: opts.version || 0 }
      : null;
  }
  // The create-on-server target is explicit (opts.address) or armed by a prior
  // newEditor({ address }) — consumed here on the first image load after it. Incognito
  // never creates on a server (central guard covering openImageHere / createBlankImage /
  // the console API). A reopen (opts.remoteId) links an existing project, never creates.
  const armedAddress = opts.address || (opts.remoteId ? null : app.pendingRemoteAddress);
  app.pendingRemoteAddress = null;   // one-shot: consumed (or cleared) by this load
  const remoteCreateAddress = (armedAddress && !opts.remoteId && !app.storage.incognito) ? armedAddress : null;
  const remoteLayout = opts.layout || null;
  const plan = { replaceInPlace, keptLines, oldImageSource, oldImageResource,
                 remoteCreateAddress, remoteLayout };

  const reader = new FileReader();
  reader.onload = event => {
    app.originalImage = new Image();
    app.originalImage.onload = () => settleLoadedImage(app, file, opts, plan);
    app.originalImage.src = event.target.result;
    // Store base64 of the ORIGINAL for persistence (the crop is stored as a
    // rectangle, never baked into the saved image).
    app.imageDataUrl = event.target.result;
  };
  reader.readAsDataURL(file);
};
