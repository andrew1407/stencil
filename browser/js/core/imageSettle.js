import { setVal, notify } from '../utils.js';
import { normalizePageSize } from './units.js';
import { validateLayout } from './layout.js';
import { normalizeHex } from './accents.js';
import { playCanvasArrival } from '../ui/motion.js';
import { requireConnection, createRemoteProject } from '../net/remoteSync.js';
import { getSyncToServer } from '../net/connectionStore.js';

// ── What a decoded image settles ────────────────────────────────
// The second half of the load flow (imageLoadFlow.js), run once the original has decoded:
// rotation + crop, which lines survive, the session commit, then the server push a
// replace-in-place or a create-on-server owes. `plan` is what the first half worked out.

// Ask the extension bridge to unpin an image we are replacing. Best-effort.
const requestUnpin = (source, resource, name) => {
  if (!source && !resource) return;   // nothing identifiable to unpin
  try {
    window.postMessage({
      source: 'stencil-editor-bridge', type: 'unpin',
      pinSource: source || '', resource: resource || source || '', name: name || '', kind: 'image',
    }, '*');
  } catch { /* postMessage unavailable (non-DOM context) — nothing to do */ }
};

// Replace the linked server project's stored `original` with `file`'s bytes, then push the
// new layout + rendered result. All-or-nothing on the sync toggle (matches edit-in-memory).
const replaceServerOriginal = async (app, file) => {
  if (!app.remoteLink || !getSyncToServer()) return;
  try {
    const conn = requireConnection(app.connections, app.remoteLink.address);
    const bytes = new Uint8Array(await file.arrayBuffer());
    await conn.putFile(app.remoteLink.remoteId, 'original', bytes, {
      ext: app.imageExt || 'png',
      w: app.originalImage ? app.originalImage.width : 0,
      h: app.originalImage ? app.originalImage.height : 0,
    });
    await app.remoteSync.saveToServer();   // push the new layout + rendered result
  } catch (err) { notify(`Could not update the server image — ${err.message}`, 'fail'); }
};

// Create the just-loaded original on `address` and link the session. Reads the
// File's raw bytes (the server is codec-free, so dimensions are passed in).
const createRemoteForSession = async (app, address, file) => {
  const conn = requireConnection(app.connections, address);
  const bytes = new Uint8Array(await file.arrayBuffer());
  app.remoteLink = await createRemoteProject(conn, {
    name: app.imageBaseName || 'Untitled',
    source: app.imageSource || '',
    resource: app.imageResource || '',
    bytes,
    ext: app.imageExt || 'png',
    w: app.originalImage ? app.originalImage.width : 0,
    h: app.originalImage ? app.originalImage.height : 0,
  });
  notify(`Saved to ${conn.url}`, 'ok');
  return app.remoteLink;
};

export const settleLoadedImage = async (app, file, opts, plan) => {
  const { replaceInPlace, keptLines, oldImageSource, oldImageResource,
          remoteCreateAddress, remoteLayout } = plan;
  // Auto-crop center to the page aspect (cut surplus sides) via album/portrait
  // detection; original kept, working canvas shows only this region. opts.crop
  // (external-launch) overrides. A reopened server project restores its saved
  // rotation + crop from the layout — rotation FIRST, because the crop rect lives
  // in rotated-original pixel space (#roundRect/defaultCropRect read rotationQuarters).
  app.rotationQuarters = (remoteLayout && Number.isInteger(remoteLayout.rotationQuarters))
    ? remoteLayout.rotationQuarters
    : 0;
  // Quick pre-load edits (load-by-URL modal): opts.page sets page size before the
  // auto-crop, opts.album forces orientation, opts.noCrop loads the full frame.
  if (opts.page) {
    const n = normalizePageSize(opts.page);
    if (n) {
      app.pageSize = n;
      setVal('page-size', n);
    }
  }
  // Restore the project's page format before the crop (defaultCropRect uses the aspect).
  if ((opts.remoteId || opts.adoptLayout) && remoteLayout) app.remoteSync.adoptServerPageFormat(remoteLayout);
  if (remoteLayout && remoteLayout.cropRect) {
    app.cropRect = app.imageModel.roundRect(remoteLayout.cropRect);
  } else if (opts.crop) {
    app.cropRect = app.imageModel.roundRect(opts.crop);
  } else if (opts.noCrop) {
    const { w: iw, h: ih } = app.imageModel.rotatedOriginalDims();
    app.cropRect = app.imageModel.roundRect({ x: 0, y: 0, width: iw, height: ih }, iw, ih);
  } else {
    app.cropRect = app.imageModel.defaultCropRect(opts.album);
  }
  app.imageModel.rebuildCroppedImage();

  // Replacing in place: keep the existing annotations (when asked) over the new image,
  // else start clean — never run the pending-lines re-upload flow.
  if (replaceInPlace) {
    app.lines = keptLines || [];
  }
  // If pending lines exist from a previous session where image couldn't be stored,
  // apply them automatically when user re-uploads an image of matching (crop) size
  else if (app.pendingLines && app.pendingLines.length > 0) {
    const ps = app.pendingImageSize;
    if (!ps || (ps.w === app.canvas.width && ps.h === app.canvas.height)) {
      app.lines = app.pendingLines;
      app.pendingLines = null;
      app.pendingImageSize = null;
      app.storage.showImageMissingBanner(false);
      app.showSaveStatus('Drawing restored!', 'var(--success)', 'check');
    } else {
      if (await app.confirm(`Saved drawing was for a ${ps.w}×${ps.h} image but this image is ${app.canvas.width}×${app.canvas.height}. Apply saved lines anyway?`, { title: 'Size mismatch' }))
        app.lines = app.pendingLines;
      app.pendingLines = null;
      app.pendingImageSize = null;
      app.storage.showImageMissingBanner(false);
    }
  } else {
    app.lines = [];
  }

  // Reopened server project (or an adoptLayout incognito copy): adopt its stored lines +
  // filter/tint (no paste prompts); no stored layout resets the filter to 'none' so the
  // prior project's filter doesn't bleed in (matches desktop openServerProject).
  if (opts.remoteId || opts.adoptLayout) {
    if (remoteLayout) {
      const verdict = validateLayout(remoteLayout, {
        hasImage: true,
        imgW: app.canvas.width,
        imgH: app.canvas.height,
        hasExistingLines: false,
      });
      if (verdict.ok) app.lines = verdict.lines;
      app.remoteSync.adoptServerFilter(remoteLayout);
      app.remoteSync.adoptServerFormulas(remoteLayout);
    } else {
      app.remoteSync.adoptServerFilter({});     // no saved filter — reset to 'none'
      app.remoteSync.adoptServerFormulas({});   // no saved formulas — reset to off
    }
  }

  app.currentLine = null;
  app.history.reset(app.lines);
  // Blank-fill colour for this session: a blank load (createBlankImage / recolour) passes it;
  // any ordinary image load clears it (opts.blankColor undefined → ""). A replace-in-place
  // recolour keeps it. This drives the meta blank/blankColor persisted by storage.save().
  if (opts.blankColor != null) app.blankColor = opts.blankColor;
  else if (!replaceInPlace) app.blankColor = '';
  // File-origin provenance: a .stencil open passes fromFile; any other fresh load clears
  // it (a replace-in-place keeps the project's existing origin).
  if (!replaceInPlace) app.fromFile = !!opts.fromFile;
  // A blank recolor keeps the SAME dimensions (setBlankColor reads them off the
  // current canvas) — nothing to refit, and doing it anyway threw away whatever
  // zoom/pan the user had. `replaceProjectImage`'s swap-in of a different file can
  // genuinely change size/aspect, so that path keeps the fit.
  if (!opts.keepZoom) app.zoomPan.fitToWindow();
  app.updateInfo();
  app.coordTable.update(app.lines.length > 0 ? app.lines[app.lines.length - 1].points : null);
  app.renderer.redraw();
  // Every fresh image gets the dust-assembly arrival (ghostIn — the clear's ghostOut
  // reversed), falling back to the drop-point flight when it can't play. Only an
  // in-place replace is exempt; callers can opt out with `landing: false`.
  if (!replaceInPlace && opts.landing !== false) {
    // Synchronously, in this same tick — a frame's delay would flash the finished
    // image before hiding it. redraw() above already filled the backing store.
    playCanvasArrival(app.canvas, { from: opts.from });
  }
  app.updateButtons();
  app.updateCoordStatus();
  app.storage.save();

  // Adopt a reopened server project's accent colour into the local meta (local-only —
  // the server already holds it), then repaint the name. Applied even when empty so a
  // peer CLEARING the colour propagates too (empty restores the neutral-grey fallback).
  if ((opts.remoteId || opts.adoptLayout) && opts.color != null && app.activeProjectId != null) {
    app.storage.store.setColor(app.activeProjectId, normalizeHex(opts.color) || '');
    app.updateProjectTitle();
  }
  // Restore a .stencil project's search keywords into the local meta (same local-only
  // adopt as the accent colour above; applied via the store so the projects list matches).
  if ((opts.remoteId || opts.adoptLayout) && Array.isArray(opts.keywords) && opts.keywords.length && app.activeProjectId != null) {
    app.storage.store.setKeywords(app.activeProjectId, opts.keywords);
  }

  // Replace-in-place post-steps: optional rename, unpin the OLD image, push the new
  // original to the server when this project is server-linked.
  if (replaceInPlace) {
    if (opts.rename) {
      if (app.activeProjectId != null) app.renameProject(app.activeProjectId, app.imageBaseName);
      app.updateProjectTitle();
    }
    requestUnpin(oldImageSource, oldImageResource, app.imageBaseName);
    if (app.remoteLink) await replaceServerOriginal(app, file);
  }

  // Create-on-server: push this just-loaded original to the chosen server and
  // link the session so later saves write back. Best-effort — a failure leaves
  // the project local and surfaces a toast.
  if (remoteCreateAddress) {
    try { await createRemoteForSession(app, remoteCreateAddress, file); }
    catch (err) { notify(`Could not save to server — ${err.message}`, 'fail'); }
  }
  app.reportIncognitoSession();   // refresh our incognito peer entry with the new name
};
