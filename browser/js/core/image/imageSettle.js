import { setVal, notify } from '../../utils.js';
import { normalizePageSize } from '../units.js';
import { validateLayout } from '../layout.js';
import { normalizeHex } from '../accents.js';
import { playCanvasArrival } from '../../ui/motion.js';
import { requireConnection, createRemoteProject } from '../../net/remoteSync.js';
import { getSyncToServer } from '../../net/connectionStore.js';

// The second half of the load flow (imageLoadFlow.js), once the original has decoded;
// `plan` is what the first half worked out.

// Best-effort.
const requestUnpin = (source, resource, name) => {
  if (!source && !resource) return;
  try {
    window.postMessage({
      source: 'stencil-editor-bridge', type: 'unpin',
      pinSource: source || '', resource: resource || source || '', name: name || '', kind: 'image',
    }, '*');
  } catch { /* postMessage unavailable (non-DOM context) — nothing to do */ }
};

// All-or-nothing on the sync toggle (matches edit-in-memory).
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

// The server is codec-free, so dimensions are passed in.
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
// Rotation FIRST: the crop rect lives in rotated-original pixel space.
  app.rotationQuarters = (remoteLayout && Number.isInteger(remoteLayout.rotationQuarters))
    ? remoteLayout.rotationQuarters
    : 0;
// Pre-load edits from the load-by-URL modal: opts.page, opts.album, opts.noCrop.
  if (opts.page) {
    const n = normalizePageSize(opts.page);
    if (n) {
      app.pageSize = n;
      setVal('page-size', n);
    }
  }
// Before the crop: defaultCropRect uses the aspect.
  if ((opts.remoteId || opts.adoptLayout) && remoteLayout) app.remoteSync.adoptServerPageFormat(remoteLayout);
  if (remoteLayout && remoteLayout.cropRect) {
    app.cropRect = app.imageModel.roundRect(remoteLayout.cropRect);
  } else if (opts.crop) {
    app.cropRect = app.imageModel.roundRect(opts.crop);
  } else if (opts.noCrop) {
    const { width: iw, height: ih } = app.imageModel.rotatedOriginalDims();
    app.cropRect = app.imageModel.roundRect({ x: 0, y: 0, width: iw, height: ih }, iw, ih);
  } else {
    app.cropRect = app.imageModel.defaultCropRect(opts.album);
  }
  app.imageModel.rebuildCroppedImage();

// Never run the pending-lines re-upload flow on an in-place replace.
  if (replaceInPlace) {
    app.lines = keptLines || [];
  }
// Pending lines from a session whose image couldn't be stored apply when the re-uploaded
// image matches the crop size.
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

// No stored layout resets the filter to 'none' so the prior project's doesn't bleed in
// (matches desktop openServerProject).
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
      app.remoteSync.adoptServerFilter({});
      app.remoteSync.adoptServerFormulas({});
    }
  }

  app.currentLine = null;
  app.history.reset(app.lines);
// A blank load passes it; an ordinary load clears it; a replace-in-place recolour keeps it.
  if (opts.blankColor != null) app.blankColor = opts.blankColor;
  else if (!replaceInPlace) app.blankColor = '';
// A replace-in-place keeps the project's existing file origin.
  if (!replaceInPlace) app.fromFile = !!opts.fromFile;
// A blank recolor keeps the SAME dimensions, so refitting would only throw away the zoom/pan.
  if (!opts.keepZoom) app.zoomPan.fitToWindow();
  app.updateInfo();
  app.coordTable.update(app.lines.length > 0 ? app.lines[app.lines.length - 1].points : null);
  app.renderer.redraw();
// The dust-assembly arrival (ghostIn); only an in-place replace is exempt.
  if (!replaceInPlace && opts.landing !== false) {
// Synchronously: a frame's delay would flash the finished image before hiding it.
    playCanvasArrival(app.canvas, { from: opts.from });
  }
  app.updateButtons();
  app.updateCoordStatus();
  app.storage.save();

// Local-only (the server already holds it); applied even when empty so a peer CLEARING
// the colour propagates too.
  if ((opts.remoteId || opts.adoptLayout) && opts.color != null && app.activeProjectId != null) {
    app.storage.store.setColor(app.activeProjectId, normalizeHex(opts.color) || '');
    app.updateProjectTitle();
  }
// Same local-only adopt as the accent colour.
  if ((opts.remoteId || opts.adoptLayout) && Array.isArray(opts.keywords) && opts.keywords.length && app.activeProjectId != null) {
    app.storage.store.setKeywords(app.activeProjectId, opts.keywords);
  }

// Replace-in-place post-steps.
  if (replaceInPlace) {
    if (opts.rename) {
      if (app.activeProjectId != null) app.renameProject(app.activeProjectId, app.imageBaseName);
      app.updateProjectTitle();
    }
    requestUnpin(oldImageSource, oldImageResource, app.imageBaseName);
    if (app.remoteLink) await replaceServerOriginal(app, file);
  }

// Best-effort: a failure leaves the project local and surfaces a toast.
  if (remoteCreateAddress) {
    try { await createRemoteForSession(app, remoteCreateAddress, file); }
    catch (err) { notify(`Could not save to server — ${err.message}`, 'fail'); }
  }
  app.reportIncognitoSession();
};
