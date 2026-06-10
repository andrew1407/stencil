import { ProjectsStore, shouldPersist } from './projectsStore.js';
import { PROJECT_ACTION } from '../worker/messages.js';
// ── Storage: thin DOM adapter over ProjectsStore for the ACTIVE project ──
// The multi-project schema lives in ProjectsStore (pure, DOM-free). This class
// is the window-side bridge: it builds the layout/payload from live app state,
// compresses the image, regenerates a thumbnail, and reads payloads back into
// the DOM. `save()` keeps its name (~40 call sites) and now writes the active
// project; in temporary mode it is a no-op.
export class Storage {
  constructor(app) {
    this.app = app;
    this.store = new ProjectsStore(localStorage);
    this.activeId = null;
    this.temporary = false;
    // Incognito: a deliberately unsaved editor. Unlike a plain temporary editor
    // (which promotes to a saved project the moment an image is loaded),
    // incognito NEVER persists — adding an image/lines stays in memory only.
    this.incognito = false;
  }

  #tempStatusTimer = null;
  #syncTimer = null;

  // Build the EXACT layout object (28 fields) from current app state.
  #buildLayout() {
    const viewport = document.getElementById('canvasViewport');
    return {
      imageWidth: this.app.canvas.width,
      imageHeight: this.app.canvas.height,
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
      tooltipEnabled: this.app.tooltipEnabled,
      tooltipShowPage: this.app.tooltipShowPage,
      tooltipShowScreen: this.app.tooltipShowScreen,
      tooltipShowCoords: this.app.tooltipShowCoords,
      allowFormulas: this.app.allowFormulas,
      formulaX: this.app.formulaX,
      formulaY: this.app.formulaY,
      drawMode: this.app.drawMode,
      selGlowColor: this.app.selGlowColor,
      hoverRingColor: this.app.hoverRingColor,
      focusRingColor: this.app.focusRingColor,
      defaultFillColor: this.app.defaultFillColor,
    };
  }

  // Persist the active project. No-op (with a throttled hint) in temp mode.
  save() {
    if (!shouldPersist(this.activeId, this.temporary)) {
      // Throttle the "not saved" hint so rapid edits don't spam the status line.
      if (!this.#tempStatusTimer) {
        this.app.showSaveStatus('● Temporary — not saved', '#e67e22');
        this.#tempStatusTimer = setTimeout(() => { this.#tempStatusTimer = null; }, 1500);
      }
      return;
    }

    const layout = this.#buildLayout();
    const meta = {
      id: this.activeId,
      name: this.app.imageBaseName || this.store.getMeta(this.activeId)?.name || 'Untitled',
      thumbnail: this.#makeThumbnail(),
      createdAt: this.store.getMeta(this.activeId)?.createdAt ?? Date.now(),
      hasImage: !!this.app.imageDataUrl,
      imageW: this.app.canvas.width,
      imageH: this.app.canvas.height,
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
      try { this.app.tabs?.projectsChanged({ id, action: PROJECT_ACTION.UPDATED }); } catch {}
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
    if (!sameImage || !this.app.image) {
      this.loadPayloadIntoApp(payload);
      this.app.showSaveStatus('↺ Synced from another tab', '#007bff');
      return;
    }
    const layout = payload.layout || {};
    this.app.lines = layout.lines || [];
    this.app.history.reset(this.app.lines, this.app.lines.length ? 0 : -1);
    if (layout.showPoints !== undefined) {
      this.app.showPoints = layout.showPoints;
      const cb = document.getElementById('showPoints');
      if (cb) cb.checked = layout.showPoints;
    }
    if (layout.showLines !== undefined) {
      this.app.showLines = layout.showLines;
      const cb = document.getElementById('showLines');
      if (cb) cb.checked = layout.showLines;
    }
    this.app.selectedLineIdx = -1;
    this.app.coordLineIdx = -1;
    this.app.focusedPtIdx = -1;
    const selPanel = document.getElementById('selectionPanel');
    if (selPanel) selPanel.style.display = 'none';
    this.app.renderer.redraw();
    this.app.updateButtons();
    this.app.coordTable.update(this.app.lines.length ? this.app.lines[this.app.lines.length - 1].points : null);
    this.app.showSaveStatus('↺ Synced from another tab', '#007bff');
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
          this.app.showSaveStatus('✓ Saved' + note, '#28a745');
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
            try { this.app.tabs?.projectsChanged(); } catch {}
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
      this.app.showSaveStatus('⚠ Lines saved — image too large for browser storage', '#e67e22');
      this.showImageMissingBanner(true);
      console.warn('Project image too large for storage; saved layout only.');
    } catch (e) {
      console.warn('Could not save project:', e);
      this.app.showSaveStatus('✗ Save failed (storage full)', '#dc3545');
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
      const max = 160;
      const iw = this.app.image.width;
      const ih = this.app.image.height;
      const scale = Math.min(1, max / Math.max(iw, ih));
      const w = Math.max(1, Math.round(iw * scale));
      const h = Math.max(1, Math.round(ih * scale));
      const offscreen = document.createElement('canvas');
      offscreen.width = w;
      offscreen.height = h;
      offscreen.getContext('2d').drawImage(this.app.image, 0, 0, w, h);
      return offscreen.toDataURL('image/jpeg', 0.6);
    } catch {
      return null;
    }
  }

  showImageMissingBanner(show) {
    let banner = document.getElementById('imageMissingBanner');
    if (!banner) {
      banner = document.createElement('div');
      banner.id = 'imageMissingBanner';
      banner.style.cssText = 'background:#fff3cd;border:2px solid #ffc107;border-radius:6px;padding:10px 16px;margin-bottom:12px;font-size:14px;color:#856404;display:flex;align-items:center;gap:10px;';
      banner.innerHTML = '⚠️ <strong>Image too large to save in browser storage.</strong> Your drawing lines are saved. Please re-upload the same image after refreshing — your lines will reappear automatically. <button onclick="document.getElementById(\'imageUpload\').click();this.closest(\'div\').style.display=\'none\'" style="margin-left:auto;padding:5px 12px;background:#ffc107;border:none;border-radius:4px;cursor:pointer;font-weight:bold;">Re-upload Image</button>';
      const selPanel = document.getElementById('selectionPanel');
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
        document.getElementById('pageSize').value = layout.pageSize;
        const cg = document.getElementById('customSizeGroup');
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
        document.getElementById('lineColor').value = layout.color;
      }
      if (layout.thickness) {
        this.app.thickness = layout.thickness;
        document.getElementById('lineThickness').value = layout.thickness;
      }
      if (layout.markerSize) {
        this.app.markerSize = layout.markerSize;
        document.getElementById('markerSize').value = layout.markerSize;
      }
      if (layout.style) {
        this.app.style = layout.style;
        document.getElementById('lineStyle').value = layout.style;
      }
      this.app.showPoints = layout.showPoints !== undefined ? layout.showPoints : true;
      this.app.showLines = layout.showLines !== undefined ? layout.showLines : true;
      this.app.imageFilter = layout.imageFilter || (layout.blackAndWhite ? 'bw' : 'none');
      if (layout.filterColor) {
        this.app.filterColor = layout.filterColor;
        const fp = document.getElementById('filterColor');
        if (fp) fp.value = this.app.filterColor;
      }
      document.getElementById('showPoints').checked = this.app.showPoints;
      document.getElementById('showLines').checked = this.app.showLines;
      document.getElementById('imageFilter').value = this.app.imageFilter;
      const filterColorPicker = document.getElementById('filterColor');
      if (filterColorPicker) filterColorPicker.style.display = (this.app.imageFilter === 'custom') ? 'inline-block' : 'none';
      this.app.imageBaseName = layout.imageBaseName || null;
      this.app.imageExt = layout.imageExt || null;
      if (layout.tooltipEnabled !== undefined) this.app.tooltipEnabled = layout.tooltipEnabled;
      if (layout.tooltipShowPage !== undefined) this.app.tooltipShowPage = layout.tooltipShowPage;
      if (layout.tooltipShowScreen !== undefined) this.app.tooltipShowScreen = layout.tooltipShowScreen;
      if (layout.tooltipShowCoords !== undefined) this.app.tooltipShowCoords = layout.tooltipShowCoords;
      if (layout.allowFormulas !== undefined) {
        this.app.allowFormulas = layout.allowFormulas;
        const cb = document.getElementById('allowFormulas');
        if (cb) cb.checked = this.app.allowFormulas;
        const ctxCb = document.getElementById('ctx-allow-formulas');
        if (ctxCb) ctxCb.checked = this.app.allowFormulas;
        const fi = document.getElementById('formulaInputs');
        const ctxFi = document.getElementById('ctx-formula-inputs');
        if (fi) fi.style.display = this.app.allowFormulas ? 'inline-flex' : 'none';
        if (ctxFi) ctxFi.style.display = this.app.allowFormulas ? 'block' : 'none';
      } else {
        this.app.allowFormulas = false;
        const cb = document.getElementById('allowFormulas');
        if (cb) cb.checked = false;
        const ctxCb = document.getElementById('ctx-allow-formulas');
        if (ctxCb) ctxCb.checked = false;
        const fi = document.getElementById('formulaInputs');
        const ctxFi = document.getElementById('ctx-formula-inputs');
        if (fi) fi.style.display = 'none';
        if (ctxFi) ctxFi.style.display = 'none';
      }
      this.app.formulaX = layout.formulaX || '';
      this.app.formulaY = layout.formulaY || '';
      {
        const el = document.getElementById('formulaX');
        const ctxEl = document.getElementById('ctx-formula-x');
        if (el) el.value = this.app.formulaX;
        if (ctxEl) ctxEl.value = this.app.formulaX;
        const ely = document.getElementById('formulaY');
        const ctxEly = document.getElementById('ctx-formula-y');
        if (ely) ely.value = this.app.formulaY;
        if (ctxEly) ctxEly.value = this.app.formulaY;
      }
      if (layout.drawMode) this.app.drawMode = layout.drawMode;
      if (layout.selGlowColor) this.app.selGlowColor = layout.selGlowColor;
      if (layout.hoverRingColor) this.app.hoverRingColor = layout.hoverRingColor;
      if (layout.focusRingColor) this.app.focusRingColor = layout.focusRingColor;
      if (layout.defaultFillColor) this.app.defaultFillColor = layout.defaultFillColor;
      this.app.syncDrawModeUI();

      // Reset any pending-image state from a prior project.
      this.app.pendingLines = null;
      this.app.pendingImageSize = null;

      if (imageDataUrl) {
        // Full restore: image + lines
        this.app.imageDataUrl = imageDataUrl;
        this.app.image = new Image();
        this.app.image.onload = () => {
          // Stale-load guard: ignore if the user switched projects mid-load.
          if (this.activeId !== targetId) return;
          this.app.canvas.width = this.app.image.width;
          this.app.canvas.height = this.app.image.height;
          this.app.lines = layout.lines || [];
          this.app.history.reset(this.app.lines, 0);

          if (layout.zoom) {
            this.app.zoomPan.setZoom(layout.zoom);
            const vp = document.getElementById('canvasViewport');
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
          this.app.showSaveStatus('↺ Project loaded', '#007bff');
        };
        this.app.image.src = imageDataUrl;
      } else if ((layout.lines || []).length > 0) {
        // Lines saved but image was too large for storage: keep lines pending.
        this.app.image = null;
        this.app.imageDataUrl = null;
        this.app.lines = [];
        this.app.history.reset([], -1);
        this.app.pendingLines = layout.lines;
        this.app.pendingImageSize = { w: layout.imageWidth, h: layout.imageHeight };
        this.app.updateInfo();
        this.app.renderer.redraw();
        this.app.updateButtons();
        this.showImageMissingBanner(true);
        this.app.showSaveStatus('⚠ Re-upload image to restore drawing', '#e67e22');
      } else {
        // Settings only.
        this.app.image = null;
        this.app.imageDataUrl = null;
        this.app.lines = [];
        this.app.history.reset([], -1);
        this.app.updateInfo();
        this.app.renderer.redraw();
        this.app.updateButtons();
        this.app.showSaveStatus('↺ Settings restored', '#007bff');
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
    this.app.imageDataUrl = null;
    this.app.imageBaseName = null;
    this.app.imageExt = null;
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
    const selPanel = document.getElementById('selectionPanel');
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

  // Compress image to JPEG at given quality; returns data URL or null
  #compressImage(quality) {
    if (!this.app.image) return null;
    try {
      const offscreen = document.createElement('canvas');
      offscreen.width = this.app.image.width;
      offscreen.height = this.app.image.height;
      offscreen.getContext('2d').drawImage(this.app.image, 0, 0);
      return offscreen.toDataURL('image/jpeg', quality);
    } catch {
      return null;
    }
  }
}
