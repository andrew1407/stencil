import { ProjectsStore, shouldPersist } from './projectsStore.js';
import { PROJECT_ACTION } from '../worker/messages.js';
import { getSyncToServer } from '../net/connectionStore.js';
// ── Storage: thin DOM adapter over ProjectsStore for the ACTIVE project ──
// Window-side bridge over the DOM-free ProjectsStore: builds the layout/payload from live
// app state, compresses the image, regenerates a thumbnail, reads payloads back into the
// DOM. `save()` writes the active project; no-op in temporary mode.
export class Storage {
  constructor(app) {
    this.app = app;
    this.store = new ProjectsStore(localStorage);
    this.activeId = null;
    this.temporary = false;
    // Incognito: a deliberately unsaved editor. Unlike a plain temporary editor (which
    // promotes to a saved project the moment an image loads), incognito NEVER persists —
    // adding an image/lines stays in memory only.
    this.incognito = false;
  }

  #tempStatusTimer = null;
  #syncTimer = null;

  // Build the EXACT layout object (28 fields) from current app state.
  #buildLayout() {
    const viewport = document.getElementById('canvas-viewport');
    return {
      imageWidth: this.app.canvas.width,
      imageHeight: this.app.canvas.height,
      // The crop rectangle (rotated-image pixels). The stored image stays the
      // untouched original; the rotation + crop are re-applied on load.
      cropRect: this.app.cropRect,
      // 90° quarter-turns (0..3, clockwise) applied to the original before the
      // crop is taken. cropRect lives in this rotated space.
      rotationQuarters: this.app.rotationQuarters || 0,
      lines: this.app.lines,
      pageSize: this.app.pageSize,
      customPageWidth: this.app.customPageWidth,
      customPageHeight: this.app.customPageHeight,
      unit: this.app.unit,
      color: this.app.color,
      thickness: this.app.thickness,
      markerSize: this.app.markerSize,
      style: this.app.style,
      showPoints: this.app.showPoints,
      showLines: this.app.showLines,
      imageFilter: this.app.imageFilter,
      filterColor: this.app.filterColor,
      zoom: this.app.scale,
      scrollLeft: viewport ? viewport.scrollLeft : 0,
      scrollTop: viewport ? viewport.scrollTop : 0,
      imageBaseName: this.app.imageBaseName || null,
      imageExt: this.app.imageExt || null,
      // Provenance: the image/video's own URL (source) and the web page it was
      // pulled from (resource). Both empty for plain local uploads; populated by
      // the add-by-URL flow and the browser extension hand-off.
      imageSource: this.app.imageSource || null,
      imageResource: this.app.imageResource || null,
      tooltipEnabled: this.app.tooltipEnabled,
      tooltipShowPage: this.app.tooltipShowPage,
      tooltipShowScreen: this.app.tooltipShowScreen,
      tooltipShowCoords: this.app.tooltipShowCoords,
      allowFormulas: this.app.allowFormulas,
      formulaX: this.app.formulaX,
      formulaY: this.app.formulaY,
      drawMode: this.app.drawMode,
      holdDrawDelay: this.app.holdDrawDelay,
      selGlowColor: this.app.selGlowColor,
      hoverRingColor: this.app.hoverRingColor,
      focusRingColor: this.app.focusRingColor,
      defaultFillColor: this.app.defaultFillColor,
    };
  }

  // Persist the active project. No-op (with a throttled hint) in temp mode.
  save() {
    // Sync off + a fetched server project = edit-in-memory only: don't persist locally
    // either (the project is "stored nowhere" — download or "Make local copy" to keep it).
    if (this.app.remoteLink && !getSyncToServer()) {
      if (!this.#tempStatusTimer) {
        this.app.showSaveStatus('Sync off — not saved', 'var(--warning)', 'info');
        this.#tempStatusTimer = setTimeout(() => { this.#tempStatusTimer = null; }, 1500);
      }
      return;
    }
    if (!shouldPersist(this.activeId, this.temporary)) {
      // Throttle the "not saved" hint so rapid edits don't spam the status line.
      if (!this.#tempStatusTimer) {
        this.app.showSaveStatus('Temporary — not saved', 'var(--warning)', 'info');
        this.#tempStatusTimer = setTimeout(() => { this.#tempStatusTimer = null; }, 1500);
      }
      return;
    }

    const layout = this.#buildLayout();
    const meta = {
      id: this.activeId,
      name: this.store.getMeta(this.activeId)?.name || this.app.imageBaseName || 'Untitled',
      thumbnail: this.#makeThumbnail(),
      createdAt: this.store.getMeta(this.activeId)?.createdAt ?? Date.now(),
      hasImage: !!this.app.imageDataUrl,
      imageW: this.app.canvas.width,
      imageH: this.app.canvas.height,
      // Mirror provenance into the registry meta so the projects list and the
      // extension-launch "resume" match can use it without reading the payload.
      source: this.app.imageSource || null,
      resource: this.app.imageResource || null,
      // Server linkage (set when this session was opened from / pushed to a server),
      // so the projects list can show this row as the SAME project as its golden
      // remote row instead of a duplicate. Null for purely-local projects.
      address: this.app.remoteLink?.address || null,
      remoteId: this.app.remoteLink?.remoteId || null,
      // Last-known server version (LWW guard), so reopening a server-linked project
      // from the list restores the link without a stale-version save conflict.
      remoteVersion: this.app.remoteLink?.version || 0,
    };
    this.#upsertWithQuota(meta, { image: this.app.imageDataUrl || null, layout });

    // Tell other tabs this project changed so any tab viewing it re-syncs its
    // editor. Debounced so a burst of edits coalesces into one broadcast.
    this.#scheduleSyncBroadcast();
  }

  // Trailing-edge debounce of the "project updated" cross-tab broadcast.
  #scheduleSyncBroadcast() {
    const id = this.activeId;
    clearTimeout(this.#syncTimer);
    this.#syncTimer = setTimeout(() => {
      try {
        this.app.tabs?.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
      } catch {
        /* coordinator gone — cross-tab sync is best-effort, the local save already succeeded */
      }
    }, 400);
  }

  // Re-read the active project from storage after another tab saved it. Uses a
  // light path (lines/view only) when the image is unchanged so line-edits don't
  // re-decode the image or reset the viewport; full reload if the image differs.
  syncActiveFromStorage() {
    if (this.activeId == null) return;
    const proj = this.store.get(this.activeId);
    if (!proj) return; // gone — removal is handled by the REMOVED action
    const payload = proj.payload || {};
    const sameImage = (payload.image || null) === (this.app.imageDataUrl || null);
    // A crop change keeps the same original image but resizes the working canvas,
    // so the light path (lines only) can't represent it — fall back to a full
    // reload when the stored crop differs from the live one.
    const sameCrop = JSON.stringify((payload.layout || {}).cropRect || null) === JSON.stringify(this.app.cropRect || null);
    if (!sameImage || !sameCrop || !this.app.image) {
      this.loadPayloadIntoApp(payload);
      this.app.showSaveStatus('Synced from another tab', 'var(--accent)', 'refresh');
      return;
    }
    const layout = payload.layout || {};
    this.app.lines = layout.lines || [];
    this.app.history.reset(this.app.lines, this.app.lines.length ? 0 : -1);
    if (layout.showPoints !== undefined) {
      this.app.showPoints = layout.showPoints;
      const cb = document.getElementById('show-points');
      if (cb) cb.checked = layout.showPoints;
    }
    if (layout.showLines !== undefined) {
      this.app.showLines = layout.showLines;
      const cb = document.getElementById('show-lines');
      if (cb) cb.checked = layout.showLines;
    }
    this.app.selectedLineIdx = -1;
    this.app.coordLineIdx = -1;
    this.app.focusedPtIdx = -1;
    const selPanel = document.getElementById('selection-panel');
    if (selPanel) selPanel.style.display = 'none';
    this.app.renderer.redraw();
    this.app.updateButtons();
    this.app.coordTable.update(this.app.lines.length ? this.app.lines[this.app.lines.length - 1].points : null);
    this.app.showSaveStatus('Synced from another tab', 'var(--accent)', 'refresh');
  }

  // Upsert with progressive image compression + eviction on quota exhaustion.
  #upsertWithQuota(meta, payload) {
    const qualities = [null, 0.85, 0.65, 0.45, 0.25]; // null = original
    const baseImage = payload.image;

    const tryStore = imageField => {
      const m = { ...meta };
      const p = { ...payload, image: imageField };
      m.hasImage = !!imageField;
      this.store.upsert(m, p);
    };

    // 1) Try each compression level; on quota, sweep expired then evict oldest
    //    OTHER projects one at a time and retry.
    for (const q of qualities) {
      const img = (baseImage == null || q === null) ? baseImage : this.#compressImage(q);
      if (baseImage != null && q !== null && !img) continue;
      let attempted = false;
      while (true) {
        try {
          tryStore(img);
          const note = q === null || baseImage == null ? '' : ` (compressed ${Math.round(q * 100)}%)`;
          this.app.showSaveStatus('Saved' + note, 'var(--success)', 'check');
          this.showImageMissingBanner(false);
          return;
        } catch (e) {
          if (!this.#isQuotaError(e)) throw e;
          if (!attempted) {
            // First quota hit at this quality: reclaim expired projects.
            this.store.sweepExpired(Date.now());
            attempted = true;
            continue;
          }
          // Then evict the oldest OTHER project (never the active one).
          if (this.#evictOldestOther()) {
            try {
              this.app.tabs?.projectsChanged();
            } catch {
              /* coordinator gone — cross-tab sync is best-effort; eviction still happened */
            }
            continue;
          }
          break; // nothing left to evict at this quality → try lower quality
        }
      }
    }

    // 2) Final fallback: store with no image so at least the lines survive.
    try {
      const m = { ...meta, hasImage: false };
      this.store.upsert(m, { ...payload, image: null });
      this.app.showSaveStatus('Lines saved — image too large for browser storage', 'var(--warning)', 'alert');
      this.showImageMissingBanner(true);
      console.warn('Project image too large for storage; saved layout only.');
    } catch (e) {
      console.warn('Could not save project:', e);
      this.app.showSaveStatus('Save failed (storage full)', 'var(--danger)', 'x');
    }
  }

  #isQuotaError(e) {
    return e && (e.name === 'QuotaExceededError' || e.name === 'NS_ERROR_DOM_QUOTA_REACHED' || e.code === 22);
  }

  // Remove the least-recently-updated project that is NOT the active one.
  // Returns true if something was evicted.
  #evictOldestOther() {
    const others = this.store.list().filter(m => m.id !== this.activeId);
    if (others.length === 0) return false;
    // list() is sorted updatedAt desc → oldest is last.
    const oldest = others[others.length - 1];
    this.store.remove(oldest.id);
    return true;
  }

  // Offscreen-canvas downscale of the current image to max 160px longest side.
  #makeThumbnail() {
    if (!this.app.image) return null;
    try {
      // Render the EDITED result (filter + lines), not the raw original, so the
      // projects list previews match what the user actually drew/filtered.
      const src = this.app.renderResultCanvas();
      const max = 160;
      const iw = src.width;
      const ih = src.height;
      const scale = Math.min(1, max / Math.max(iw, ih));
      const w = Math.max(1, Math.round(iw * scale));
      const h = Math.max(1, Math.round(ih * scale));
      const offscreen = document.createElement('canvas');
      offscreen.width = w;
      offscreen.height = h;
      offscreen.getContext('2d').drawImage(src, 0, 0, w, h);
      return offscreen.toDataURL('image/jpeg', 0.6);
    } catch {
      return null;
    }
  }

  showImageMissingBanner(show) {
    let banner = document.getElementById('image-missing-banner');
    if (!banner) {
      banner = document.createElement('div');
      banner.id = 'image-missing-banner';
      banner.style.cssText = 'background:#fff3cd;border:2px solid #ffc107;border-radius:6px;padding:10px 16px;margin-bottom:12px;font-size:14px;color:#856404;display:flex;align-items:center;gap:10px;';
      banner.innerHTML = '⚠️ <strong>Image too large to save in browser storage.</strong> Your drawing lines are saved. Please re-upload the same image after refreshing — your lines will reappear automatically. <button onclick="document.getElementById(\'imageUpload\').click();this.closest(\'div\').style.display=\'none\'" style="margin-left:auto;padding:5px 12px;background:#ffc107;border:none;border-radius:4px;cursor:pointer;font-weight:bold;">Re-upload Image</button>';
      const selPanel = document.getElementById('selection-panel');
      selPanel.parentNode.insertBefore(banner, selPanel);
    }
    banner.style.display = show ? 'flex' : 'none';
  }

  // Boot-time: migrate legacy single-project keys + sweep expired projects.
  // Does NOT auto-load a project — the projects component/coordinator decides.
  restore() {
    const now = Date.now();
    try {
      this.store.migrateLegacy(now);
      this.store.sweepExpired(now);
    } catch (e) {
      console.warn('Could not initialize project storage:', e);
    }
  }

  // Load a saved project into the live app (DOM rebuild). Guards against stale
  // async image loads if the active project changes mid-load.
  loadProject(id) {
    const proj = this.store.get(id);
    if (!proj) return false;
    this.activeId = id;
    this.temporary = false;
    this.incognito = false;
    this.app.activeProjectId = id;
    this.loadPayloadIntoApp(proj.payload);
    return true;
  }

  // Apply a payload {image, layout} into app state + DOM. Factored out of the
  // legacy restore() so both loadProject() and migration paths share it.
  loadPayloadIntoApp(payload) {
    try {
      const layout = (payload && payload.layout) || {};
      const imageDataUrl = (payload && payload.image) || null;
      const targetId = this.activeId; // capture for stale-load guard

      // Restore UI settings
      if (layout.pageSize) {
        this.app.pageSize = layout.pageSize;
        document.getElementById('page-size').value = layout.pageSize;
        const cg = document.getElementById('custom-size-group');
        if (cg) cg.style.display = layout.pageSize === 'custom' ? 'inline-flex' : 'none';
      }
      if (layout.customPageWidth) this.app.customPageWidth = layout.customPageWidth;
      if (layout.customPageHeight) this.app.customPageHeight = layout.customPageHeight;
      // Restore the display unit, then render the custom page inputs + labels in
      // it (stored values are cm; applyUnitToUI converts for display).
      if (layout.unit) this.app.unit = layout.unit;
      this.app.applyUnitToUI();
      if (layout.color) {
        this.app.color = layout.color;
        document.getElementById('line-color').value = layout.color;
      }
      if (layout.thickness) {
        this.app.thickness = layout.thickness;
        document.getElementById('line-thickness').value = layout.thickness;
      }
      if (layout.markerSize) {
        this.app.markerSize = layout.markerSize;
        document.getElementById('marker-size').value = layout.markerSize;
      }
      if (layout.style) {
        this.app.style = layout.style;
        document.getElementById('line-style').value = layout.style;
      }
      this.app.showPoints = layout.showPoints !== undefined ? layout.showPoints : true;
      this.app.showLines = layout.showLines !== undefined ? layout.showLines : true;
      this.app.imageFilter = layout.imageFilter || (layout.blackAndWhite ? 'bw' : 'none');
      if (layout.filterColor) {
        this.app.filterColor = layout.filterColor;
        const fp = document.getElementById('filter-color');
        if (fp) fp.value = this.app.filterColor;
      }
      document.getElementById('show-points').checked = this.app.showPoints;
      document.getElementById('show-lines').checked = this.app.showLines;
      document.getElementById('image-filter').value = this.app.imageFilter;
      const filterColorPicker = document.getElementById('filter-color');
      if (filterColorPicker) filterColorPicker.style.display = (this.app.imageFilter === 'custom') ? 'inline-block' : 'none';
      this.app.imageBaseName = layout.imageBaseName || null;
      this.app.imageExt = layout.imageExt || null;
      this.app.imageSource = layout.imageSource || null;
      this.app.imageResource = layout.imageResource || null;
      if (layout.tooltipEnabled !== undefined) this.app.tooltipEnabled = layout.tooltipEnabled;
      if (layout.tooltipShowPage !== undefined) this.app.tooltipShowPage = layout.tooltipShowPage;
      if (layout.tooltipShowScreen !== undefined) this.app.tooltipShowScreen = layout.tooltipShowScreen;
      if (layout.tooltipShowCoords !== undefined) this.app.tooltipShowCoords = layout.tooltipShowCoords;
      if (layout.allowFormulas !== undefined) {
        this.app.allowFormulas = layout.allowFormulas;
        const cb = document.getElementById('allow-formulas');
        if (cb) cb.checked = this.app.allowFormulas;
        const ctxCb = document.getElementById('ctx-allow-formulas');
        if (ctxCb) ctxCb.checked = this.app.allowFormulas;
        const fi = document.getElementById('formula-inputs');
        const ctxFi = document.getElementById('ctx-formula-inputs');
        if (fi) fi.style.display = this.app.allowFormulas ? 'inline-flex' : 'none';
        if (ctxFi) ctxFi.style.display = this.app.allowFormulas ? 'block' : 'none';
      } else {
        this.app.allowFormulas = false;
        const cb = document.getElementById('allow-formulas');
        if (cb) cb.checked = false;
        const ctxCb = document.getElementById('ctx-allow-formulas');
        if (ctxCb) ctxCb.checked = false;
        const fi = document.getElementById('formula-inputs');
        const ctxFi = document.getElementById('ctx-formula-inputs');
        if (fi) fi.style.display = 'none';
        if (ctxFi) ctxFi.style.display = 'none';
      }
      this.app.formulaX = layout.formulaX || '';
      this.app.formulaY = layout.formulaY || '';
      {
        const el = document.getElementById('formula-x');
        const ctxEl = document.getElementById('ctx-formula-x');
        if (el) el.value = this.app.formulaX;
        if (ctxEl) ctxEl.value = this.app.formulaX;
        const ely = document.getElementById('formula-y');
        const ctxEly = document.getElementById('ctx-formula-y');
        if (ely) ely.value = this.app.formulaY;
        if (ctxEly) ctxEly.value = this.app.formulaY;
      }
      if (layout.drawMode) this.app.drawMode = layout.drawMode;
      if (Number.isFinite(layout.holdDrawDelay))
        this.app.setHoldDrawDelay(layout.holdDrawDelay, { persist: false });   // clamps in one place
      if (layout.selGlowColor) this.app.selGlowColor = layout.selGlowColor;
      if (layout.hoverRingColor) this.app.hoverRingColor = layout.hoverRingColor;
      if (layout.focusRingColor) this.app.focusRingColor = layout.focusRingColor;
      if (layout.defaultFillColor) this.app.defaultFillColor = layout.defaultFillColor;
      this.app.syncDrawModeUI();

      // Reset any pending-image state from a prior project.
      this.app.pendingLines = null;
      this.app.pendingImageSize = null;

      if (imageDataUrl) {
        // Full restore: original image + crop + lines
        this.app.imageDataUrl = imageDataUrl;
        this.app.originalImage = new Image();
        this.app.originalImage.onload = () => {
          // Stale-load guard: ignore if the user switched projects mid-load.
          if (this.activeId !== targetId) return;
          // Re-apply the stored rotation + crop (or default-crop legacy projects
          // saved before cropping existed) and build the working canvas from it.
          // Rotation must be set first: defaultCropRect and rebuild both read it.
          this.app.rotationQuarters = layout.rotationQuarters || 0;
          this.app.cropRect = layout.cropRect || this.app.defaultCropRect();
          this.app.rebuildCroppedImage();
          this.app.lines = layout.lines || [];
          // Empty lines → step -1 (no phantom undo on a brand-new/blank project); only seed
          // a current snapshot when there are real lines to undo back to (matches line ~156).
          this.app.history.reset(this.app.lines, this.app.lines.length ? 0 : -1);

          if (layout.zoom) {
            this.app.zoomPan.setZoom(layout.zoom);
            const vp = document.getElementById('canvas-viewport');
            if (vp) {
              const availH = Math.max(200, window.innerHeight - 220);
              const fittedH = Math.round(this.app.image.height * this.app.scale);
              vp.style.maxHeight = Math.min(fittedH + 4, availH) + 'px';
              requestAnimationFrame(() => {
                if (this.activeId !== targetId) return;
                vp.scrollLeft = layout.scrollLeft || 0;
                vp.scrollTop = layout.scrollTop || 0;
              });
            }
          } else {
            this.app.zoomPan.fitToWindow();
          }

          this.app.updateInfo();
          this.app.renderer.redraw();
          this.app.updateButtons();
          this.app.updateCoordStatus();
          if (this.app.lines.length > 0)
            this.app.coordTable.update(this.app.lines[this.app.lines.length - 1].points);
          this.showImageMissingBanner(false);
          this.app.showSaveStatus('Project loaded', 'var(--accent)', 'refresh');
        };
        this.app.originalImage.src = imageDataUrl;
      } else if ((layout.lines || []).length > 0) {
        // Lines saved but image was too large for storage: keep lines pending.
        this.app.image = null;
        this.app.originalImage = null;
        this.app.cropRect = null;
        this.app.rotationQuarters = 0;
        this.app.imageDataUrl = null;
        this.app.lines = [];
        this.app.history.reset([], -1);
        this.app.pendingLines = layout.lines;
        this.app.pendingImageSize = { w: layout.imageWidth, h: layout.imageHeight };
        this.app.updateInfo();
        this.app.renderer.redraw();
        this.app.updateButtons();
        this.showImageMissingBanner(true);
        this.app.showSaveStatus('Re-upload image to restore drawing', 'var(--warning)', 'alert');
      } else {
        // Settings only.
        this.app.image = null;
        this.app.originalImage = null;
        this.app.cropRect = null;
        this.app.rotationQuarters = 0;
        this.app.imageDataUrl = null;
        this.app.lines = [];
        this.app.history.reset([], -1);
        this.app.updateInfo();
        this.app.renderer.redraw();
        this.app.updateButtons();
        this.app.showSaveStatus('Settings restored', 'var(--accent)', 'refresh');
      }
    } catch (e) {
      console.warn('Could not load project payload:', e);
    }
  }

  // Switch the editor to a fresh, blank, unsaved state. No storage writes.
  newTemporary() {
    this.activeId = null;
    this.temporary = true;
    this.incognito = false;
    this.app.activeProjectId = null;

    this.app.image = null;
    this.app.originalImage = null;
    this.app.cropRect = null;
    this.app.imageDataUrl = null;
    this.app.imageBaseName = null;
    this.app.imageExt = null;
    this.app.imageSource = null;
    this.app.imageResource = null;
    this.app.lines = [];
    this.app.currentLine = null;
    this.app.selectedLineIdx = -1;
    this.app.coordLineIdx = -1;
    this.app.focusedPtIdx = -1;
    this.app.pendingLines = null;
    this.app.pendingImageSize = null;
    this.app.history.reset([], -1);

    const ctx = this.app.ctx;
    if (ctx) ctx.clearRect(0, 0, this.app.canvas.width, this.app.canvas.height);
    const selPanel = document.getElementById('selection-panel');
    if (selPanel) selPanel.style.display = 'none';
    const fsPanel = document.getElementById('fs-selection-panel');
    if (fsPanel) fsPanel.style.display = 'none';
    this.showImageMissingBanner(false);

    this.app.updateInfo();
    this.app.updateButtons();
    this.app.coordTable.update();
    this.app.renderer.redraw();
  }

  // A temp editor just received its first image → promote to a real project.
  // The caller subsequently calls save() to persist.
  promoteTemporaryToProject() {
    this.activeId = this.store.createId();
    this.temporary = false;
    this.app.activeProjectId = this.activeId;
    return this.activeId;
  }

  // Compress image to JPEG at given quality; returns data URL or null. Operates
  // on the ORIGINAL (full) image so the stored image is never the cropped view —
  // the crop is persisted separately as a rectangle and re-applied on load.
  #compressImage(quality) {
    const src = this.app.originalImage || this.app.image;
    if (!src) return null;
    try {
      const offscreen = document.createElement('canvas');
      offscreen.width = src.width;
      offscreen.height = src.height;
      offscreen.getContext('2d').drawImage(src, 0, 0);
      return offscreen.toDataURL('image/jpeg', quality);
    } catch {
      return null;
    }
  }
}
