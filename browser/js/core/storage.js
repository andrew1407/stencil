import { ProjectsStore, shouldPersist, addPeriod, DEFAULT_PERIOD } from './projectsStore.js';
import { getProjectsBackend } from './projectsBackend.js';
import { PROJECT_ACTION } from '../worker/messages.js';
import { getSyncToServer } from '../net/connectionStore.js';
import { normalizePageSize } from './units.js';
import { normalizeCropRect } from './layout.js';
import { createTrailingSave } from './zoomPan.js';
import { ghostOut, flashLanding, playCanvasArrival, revealControls, GHOST_MS } from '../ui/motion.js';
import { setChecked } from '../ui/controlSwap.js';
import { showImageMissingBanner as paintImageMissingBanner } from '../ui/imageMissingBanner.js';
import { paintPageSize, paintDrawingControls, paintVisibilityChecks, paintFormulaFields,
         hideSelectionPanels, resetViewportScroll, scrollViewportTo } from '../ui/layoutControls.js';
import { upsertWithQuota } from './quotaWriter.js';
import { buildLayoutState, buildProjectMeta } from './projectMeta.js';
import { createThumbnailScheduler } from './thumbnail.js';

// Thin DOM adapter over the DOM-free ProjectsStore for the ACTIVE project: builds the
// payload from live app state and reads payloads back into the DOM. `save()` is a no-op
// in temporary mode.
export class Storage {
  constructor(app) {
    this.app = app;
    // Payload keys ride an IndexedDB mirror, the registry stays in localStorage (projectsBackend.js).
    const backend = getProjectsBackend();
    this.store = new ProjectsStore(backend);
    // A failed async IndexedDB persist surfaces on the save-status line (the mirror holds the bytes).
    if (backend && 'onWriteError' in backend) {
      backend.onWriteError = () =>
        this.app.showSaveStatus('Save failed (browser storage error)', 'var(--danger)', 'x');
    }
    this.activeId = null;
    this.temporary = false;
    // Incognito NEVER persists — unlike a temporary editor, which promotes to a project when an image loads.
    this.incognito = false;
    // saveHistory() rides this trailing window; flush() forces one now.
    this.saveSoon = createTrailingSave(() => this.save());
    this.thumbs = createThumbnailScheduler(this);
  }

  #tempStatusTimer = null;
  #syncTimer = null;

  // No-op (with a throttled hint) in temp mode.
  save() {
    // Nothing persists when sync is off on a fetched server project, or in a temporary editor.
    const blocked = (this.app.remoteLink && !getSyncToServer()) ? 'Sync off — not saved'
      : (shouldPersist(this.activeId, this.temporary) ? null : 'Temporary — not saved');
    if (blocked) {
      if (!this.#tempStatusTimer) {
        this.app.showSaveStatus(blocked, 'var(--warning)', 'info');
        this.#tempStatusTimer = setTimeout(() => { this.#tempStatusTimer = null; }, 1500);
      }
      return;
    }

    const layout = buildLayoutState(this.app);
    const prev = this.store.getMeta(this.activeId) || {};
    // The row keeps its last thumbnail; a fresh one renders in idle time (thumbnail.js).
    const meta = buildProjectMeta(this.app, { prev, id: this.activeId, layout, thumbnail: prev.thumbnail ?? null });
    upsertWithQuota(this, meta, { image: this.app.imageDataUrl || null, layout });
    this.thumbs.schedule(this.activeId);

    // Debounced so a burst of edits coalesces into one cross-tab broadcast.
    this.#scheduleSyncBroadcast();
    this.app.stencilSync?.onEdit();
  }

  #scheduleSyncBroadcast() {
    const id = this.activeId;
    clearTimeout(this.#syncTimer);
    this.#syncTimer = setTimeout(() => {
      try {
        this.app.tabs?.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
      } catch {
        /* coordinator gone — cross-tab sync is best-effort */
      }
    }, 400);
  }

  // After another tab saved: the light path (lines/view only) when the image is unchanged,
  // so line-edits don't re-decode the image or reset the viewport.
  syncActiveFromStorage() {
    if (this.activeId == null) return;
    const proj = this.store.get(this.activeId);
    if (!proj) return;
    const payload = proj.payload || {};
    const sameImage = (payload.image || null) === (this.app.imageDataUrl || null);
    // A crop change keeps the image but resizes the canvas, so it needs the full reload;
    // both sides normalized so a canonical {w,h} stored rect compares equal.
    const sameCrop = JSON.stringify(normalizeCropRect((payload.layout || {}).cropRect)) === JSON.stringify(normalizeCropRect(this.app.cropRect));
    if (!sameImage || !sameCrop || !this.app.image) {
      this.loadPayloadIntoApp(payload);
      this.app.showSaveStatus('Synced from another tab', 'var(--accent)', 'refresh');
      return;
    }
    const layout = payload.layout || {};
    this.app.lines = layout.lines || [];
    this.app.history.reset(this.app.lines, this.app.lines.length ? 0 : -1);
    if (layout.showPoints !== undefined) this.app.showPoints = layout.showPoints;
    if (layout.showLines !== undefined) this.app.showLines = layout.showLines;
    paintVisibilityChecks(this.app);
    this.app.selectedLineIdx = -1;
    this.app.coordLineIdx = -1;
    this.app.focusedPtIdx = -1;
    hideSelectionPanels();
    this.app.renderer.redraw();
    this.app.updateButtons();
    this.app.coordTable.update(this.app.lines.length ? this.app.lines[this.app.lines.length - 1].points : null);
    this.app.showSaveStatus('Synced from another tab', 'var(--accent)', 'refresh');
  }

  showImageMissingBanner(show) { paintImageMissingBanner(show); }

  // Boot-time migration + expiry sweep. Does NOT auto-load a project.
  restore() {
    const now = Date.now();
    try {
      this.store.migrateLegacy(now);
      this.store.sweepExpired(now);
    } catch (e) {
      console.warn('Could not initialize project storage:', e);
    }
  }

  // Guards against stale async image loads if the active project changes mid-load.
  loadProject(id) {
    const proj = this.store.get(id);
    if (!proj) return false;
    this.saveSoon.flush();
    this.thumbs.flush();
    this.activeId = id;
    this.temporary = false;
    this.incognito = false;
    this.app.activeProjectId = id;
    // An OPEN arrives the way a loaded file does; the cross-tab sync path shares this method
    // and stays still (a peer's edit must not dissolve the picture under the reader).
    this.loadPayloadIntoApp(proj.payload, { landing: true });
    this.#autoRefreshOnOpen(id);
    return true;
  }

  // "Refresh on open": expiresAt = now + its refresh period; keep-forever (0) is left alone.
  #autoRefreshOnOpen(id) {
    const meta = this.store.getMeta(id);
    if (!meta || !meta.autoRefresh || !meta.expiresAt) return;
    const period = meta.refreshPeriod || DEFAULT_PERIOD;
    this.store.setExpiration(id, { expiresAt: addPeriod(Date.now(), period), refreshPeriod: period });
    this.#scheduleSyncBroadcast();
  }

  // `landing` plays the arrival once the image is on the canvas — an explicit open only.
  loadPayloadIntoApp(payload, { landing = false } = {}) {
    try {
      const layout = (payload && payload.layout) || {};
      const imageDataUrl = (payload && payload.image) || null;
      const targetId = this.activeId;

      // The same validator as the other adoption paths: an unknown stored name keeps the
      // current page rather than poisoning getPageDimensions.
      const pageSize = normalizePageSize(layout.pageSize);
      if (pageSize) {
        this.app.pageSize = pageSize;
        paintPageSize(pageSize);
      }
      if (layout.customPageWidth) this.app.customPageWidth = layout.customPageWidth;
      if (layout.customPageHeight) this.app.customPageHeight = layout.customPageHeight;
      // Stored values are cm; applyUnitToUI converts for display.
      if (layout.unit) this.app.unit = layout.unit;
      this.app.applyUnitToUI();
      if (layout.color) this.app.color = layout.color;
      // '' is MEANINGFUL ("points follow the line colour"), hence the typeof check.
      if (typeof layout.pointColor === 'string') this.app.pointColor = layout.pointColor;
      if (layout.thickness) this.app.thickness = layout.thickness;
      if (layout.pointSize) this.app.pointSize = layout.pointSize;
      if (layout.style) this.app.style = layout.style;
      this.app.showPoints = layout.showPoints !== undefined ? layout.showPoints : true;
      this.app.showLines = layout.showLines !== undefined ? layout.showLines : true;
      this.app.imageFilter = layout.imageFilter || (layout.blackAndWhite ? 'bw' : 'none');
      if (layout.filterColor) this.app.filterColor = layout.filterColor;
      paintDrawingControls(this.app, layout);
      this.app.imageBaseName = layout.imageBaseName || null;
      this.app.imageExt = layout.imageExt || null;
      this.app.imageSource = layout.imageSource || null;
      this.app.imageResource = layout.imageResource || null;
      if (layout.tooltipEnabled !== undefined) this.app.tooltipEnabled = layout.tooltipEnabled;
      if (layout.tooltipShowPage !== undefined) this.app.tooltipShowPage = layout.tooltipShowPage;
      if (layout.tooltipShowScreen !== undefined) this.app.tooltipShowScreen = layout.tooltipShowScreen;
      if (layout.tooltipShowCoords !== undefined) this.app.tooltipShowCoords = layout.tooltipShowCoords;
      // The shared sync (settingsController.js) keeps the toolbar pill's .on class in step.
      this.app.allowFormulas = layout.allowFormulas !== undefined ? layout.allowFormulas : false;
      this.app.settings.syncFormulaUI(this.app.allowFormulas);
      this.app.formulaX = layout.formulaX || '';
      this.app.formulaY = layout.formulaY || '';
      paintFormulaFields(this.app);
      if (layout.drawMode) this.app.drawMode = layout.drawMode;
      if (Number.isFinite(layout.holdDrawDelay))
        this.app.input.setHoldDrawDelay(layout.holdDrawDelay, { persist: false });
      if (layout.selGlowColor) this.app.selGlowColor = layout.selGlowColor;
      if (layout.hoverRingColor) this.app.hoverRingColor = layout.hoverRingColor;
      if (layout.focusRingColor) this.app.focusRingColor = layout.focusRingColor;
      if (layout.defaultFillColor) this.app.defaultFillColor = layout.defaultFillColor;
      this.app.syncDrawModeUI();

      this.app.pendingLines = null;
      this.app.pendingImageSize = null;

      if (imageDataUrl) {
        this.app.imageDataUrl = imageDataUrl;
        this.app.originalImage = new Image();
        this.app.originalImage.onload = () => {
          // The user may have switched projects mid-load.
          if (this.activeId !== targetId) return;
          // Rotation first: defaultCropRect and rebuild both read it.
          this.app.rotationQuarters = layout.rotationQuarters || 0;
          this.app.cropRect = normalizeCropRect(layout.cropRect) || this.app.imageModel.defaultCropRect();
          this.app.imageModel.rebuildCroppedImage();
          this.app.lines = layout.lines || [];
          // Empty lines → step -1, so a brand-new project has no phantom undo.
          this.app.history.reset(this.app.lines, this.app.lines.length ? 0 : -1);

          if (layout.zoom) {
            this.app.zoomPan.setZoom(layout.zoom);
            // BEFORE the arrival, so the dust forms over the slice the user will see.
            scrollViewportTo(layout.scrollLeft, layout.scrollTop);
          } else {
            this.app.zoomPan.fitToWindow();
          }

          this.app.updateInfo();
          this.app.renderer.redraw();
          // Same beat as the file loader: in THIS tick, or a frame of the finished image flashes first.
          if (landing) playCanvasArrival(this.app.canvas);
          this.app.updateButtons();
          this.app.updateCoordStatus();
          if (this.app.lines.length > 0)
            this.app.coordTable.update(this.app.lines[this.app.lines.length - 1].points);
          this.showImageMissingBanner(false);
          this.app.showSaveStatus('Project loaded', 'var(--accent)', 'refresh');
        };
        this.app.originalImage.src = imageDataUrl;
      } else if ((layout.lines || []).length > 0) {
        // The image was too large for storage: keep the lines pending.
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

  // Fresh, blank, unsaved; no storage writes. `keepChat` is for the assistant resetting the
  // editor MID-TURN (openUrl incognito adoption): swapping the chat scope would wipe the
  // exchange that asked for the reset.
  newTemporary({ keepChat = false } = {}) {
    this.saveSoon.flush(); this.thumbs.flush();
    this.activeId = null;
    this.temporary = true;
    this.incognito = false;
    this.app.activeProjectId = null;
    // Chat persistence (§12): no project to file a chat under → a fresh scope.
    if (!keepChat) this.app.chatPersistence?.projectOpened(null);

    // Boot starts blank: no dust/hold when there was nothing on screen.
    const hadImage = !!this.app.image;

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
    // Copy the pixels into a throwaway overlay FIRST (clearRect leaves nothing to animate)
    // and hold the empty state back while motes are in front of it — only when ghostOut
    // says so (the arrival's ghostIn contract); under reduced motion nothing falls.
    if (ctx && hadImage && ghostOut(this.app.canvas)) {
      const vp = document.getElementById('canvas-viewport');
      if (vp) flashLanding(vp, 'canvas-clearing', GHOST_MS);
    }
    if (ctx) ctx.clearRect(0, 0, this.app.canvas.width, this.app.canvas.height);
    // Collapse the backing store + inline CSS size, or the "+ Blank image" card lands off-centre.
    this.app.canvas.width = 0;
    this.app.canvas.height = 0;
    this.app.canvas.style.width = '';
    this.app.canvas.style.height = '';
    this.app.scale = 1;
    this.app.renderedScale = null;
    this.app.zoomPan?.setZoomInputValue(100);
    resetViewportScroll();
    hideSelectionPanels();
    this.showImageMissingBanner(false);

    this.app.updateInfo();
    this.app.updateButtons();
    this.app.coordTable.update();
    this.app.renderer.redraw();
  }

  // A temp editor received its first image → a real project. The caller then calls save().
  promoteTemporaryToProject() {
    this.activeId = this.store.createId();
    this.temporary = false;
    this.app.activeProjectId = this.activeId;
    return this.activeId;
  }

}
