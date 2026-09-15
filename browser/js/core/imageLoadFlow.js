import { settleLoadedImage } from './imageSettle.js';

// DrawingApp.loadImageFromFile's body: the session bookkeeping a load does up front, then
// the decode. What the decoded image settles is imageSettle.js.

// loadImageFromFile decodes async with no promise; poll until the image is in place.
// `previous` = the image loaded BEFORE the call, so a REPLACE waits for the swap — not for
// "some image exists", which would run chained ops against the old picture.
export const waitForImage = (app, { timeoutMs = 8000, previous = null } = {}) => new Promise((resolve) => {
  const start = Date.now();
  const again = typeof requestAnimationFrame === 'function'
    ? requestAnimationFrame : (fn) => setTimeout(fn, 16);   // node --test has no rAF
  const tick = () => {
    if ((app.image && app.image !== previous) || Date.now() - start > timeoutMs) resolve();
    else again(tick);
  };
  tick();
});

export const loadImageFromFile = (app, file, opts = {}) => {
  const replaceInPlace = !!opts.replaceInPlace;
// A fresh load starts a DIFFERENT project: drop the previous .stencil file link, or
// live-sync auto-save would write the new project into the old file. A .stencil open
// re-links right after; an in-place replace keeps the same project + link.
  if (!replaceInPlace) app.stencilSync?.unlink();
// Captured before the swap: the annotations to keep and the OLD image's pin identity to clear.
  const keptLines = (replaceInPlace && opts.keepAnnotations) ? app.lines : null;
  const oldImageSource = app.imageSource;
  const oldImageResource = app.imageResource;
// A different server project becomes its own local project: flush the active one and reset
// so the promote below makes a distinct record.
  const switchingRemote = !!opts.remoteId
    && (!app.remoteLink || app.remoteLink.remoteId !== opts.remoteId);
  if (switchingRemote && !app.storage.temporary && app.activeProjectId != null
      && !app.storage.incognito) {
    app.storage.save();
    app.storage.newTemporary();
    app.activeProjectId = null;
  }
// A temporary editor receiving its first image becomes a real project; incognito editors
// stay unsaved and are never promoted.
  if (!replaceInPlace && (app.storage.temporary || app.activeProjectId == null) && !app.storage.incognito) {
    app.storage.promoteTemporaryToProject();
    app.tabs.reportActive(app.activeProjectId);
  }

  const dotIdx = file.name.lastIndexOf('.');
  if (dotIdx !== -1) {
    app.imageBaseName = file.name.slice(0, dotIdx);
    app.imageExt = file.name.slice(dotIdx + 1).toLowerCase();
  } else {
    app.imageBaseName = file.name;
    app.imageExt = 'png';
  }

// An explicit project name (extension copy-numbering) overrides the filename-derived base.
  if (opts.name) app.imageBaseName = opts.name;

// A plain local upload passes neither, clearing any provenance a prior image carried.
  app.imageSource = opts.source || null;
  app.imageResource = opts.resource || null;

// opts.remoteId → reopen an EXISTING server project; opts.address WITHOUT a remoteId →
// CREATE this image on that server after it loads. An in-place replace keeps the link.
  if (!replaceInPlace) {
    app.remoteLink = (opts.address && opts.remoteId)
      ? { address: opts.address, remoteId: opts.remoteId, version: opts.version || 0 }
      : null;
  }
// The create target is explicit (opts.address) or armed by a prior newEditor({ address }).
// Incognito never creates on a server; a reopen links an existing project, never creates.
  const armedAddress = opts.address || (opts.remoteId ? null : app.pendingRemoteAddress);
  app.pendingRemoteAddress = null;
  const remoteCreateAddress = (armedAddress && !opts.remoteId && !app.storage.incognito) ? armedAddress : null;
  const remoteLayout = opts.layout || null;
  const plan = { replaceInPlace, keptLines, oldImageSource, oldImageResource,
                 remoteCreateAddress, remoteLayout };

  const reader = new FileReader();
  reader.onload = event => {
    app.originalImage = new Image();
    app.originalImage.onload = () => settleLoadedImage(app, file, opts, plan);
    app.originalImage.src = event.target.result;
// The ORIGINAL's base64 is what persists (the crop is stored as a rect, never baked in).
    app.imageDataUrl = event.target.result;
  };
  reader.readAsDataURL(file);
};
