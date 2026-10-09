import { notify } from '../../utils.js';
import { settleLoadedImage } from './settle.js';
import { imageTooLarge, IMAGE_TOO_LARGE } from './decodeLimit.js';

// Loading a picture into the editor: the decode, then the session bookkeeping a load does, then
// what the decoded image settles (settle.js).

// loadImageFromFile decodes async with no promise, so poll. `previous` = the image before the
// call, so a REPLACE waits for the swap, not for "some image exists" (the old picture).
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

// Runs only once the picture decoded under the cap: a refused file leaves the editor as it was.
const beginLoad = (app, file, opts, fileLink) => {
  const replaceInPlace = !!opts.replaceInPlace;
// A fresh load starts a DIFFERENT project: drop the previous .stencil file link, or live-sync
// auto-save would write the new project into the old file. A link made since the call is this one's.
  if (!replaceInPlace && (app.stencilSync?.handle ?? null) === fileLink) app.stencilSync?.unlink();
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
  return { replaceInPlace, keptLines, oldImageSource, oldImageResource,
           remoteCreateAddress, remoteLayout };
};

// The newest load per editor wins: an older picture that decodes later is dropped.
const latestLoad = new WeakMap();

export const loadImageFromFile = (app, file, opts = {}) => {
  const token = {};
  latestLoad.set(app, token);
  const fileLink = app.stencilSync?.handle ?? null;
  const reader = new FileReader();
  reader.onload = event => {
    const img = new Image();
    img.onload = () => {
      if (latestLoad.get(app) !== token) return;
      if (imageTooLarge(img.naturalWidth || img.width, img.naturalHeight || img.height)) {
        notify(IMAGE_TOO_LARGE, 'fail');
        return;
      }
      const plan = beginLoad(app, file, opts, fileLink);
      app.originalImage = img;
// The ORIGINAL's base64 is what persists (the crop is stored as a rect, never baked in).
      app.imageDataUrl = event.target.result;
      settleLoadedImage(app, file, opts, plan)
        .catch((err) => notify(`Could not open the image: ${err.message || err}`, 'fail'));
    };
    img.src = event.target.result;
  };
  reader.readAsDataURL(file);
};
