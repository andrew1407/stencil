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

// ── Storage: thin DOM adapter over ProjectsStore for the ACTIVE project ──
// Window-side bridge over the DOM-free ProjectsStore: builds the layout/payload from live
// app state, compresses the image, regenerates a thumbnail, reads payloads back into the
// DOM. `save()` writes the active project; no-op in temporary mode.
export class Storage {
  constructor(app) {
    this.app = app;
    // Backend: payload keys ride an IndexedDB mirror, the registry stays in
    // localStorage (see projectsBackend.js). Hydrated at boot by index.js.
    const backend = getProjectsBackend();
    this.store = new ProjectsStore(backend);
    // Payload writes land in the sync mirror and persist to IndexedDB async — surface a
    // failed persist on the save-status line (the mirror holds the bytes; the next save retries).
    if (backend && 'onWriteError' in backend) {
      backend.onWriteError = () =>
        this.app.showSaveStatus('Save failed (browser storage error)', 'var(--danger)', 'x');
    }
    this.activeId = null;
    this.temporary = false;
    // Incognito: a deliberately unsaved editor. Unlike a plain temporary editor (which
    // promotes to a saved project the moment an image loads), incognito NEVER persists —
    // adding an image/lines stays in memory only.
    this.incognito = false;
    // saveHistory() rides this trailing window, like the zoom persist; flush() forces one now.
    this.saveSoon = createTrailingSave(() => this.save());
    this.thumbs = createThumbnailScheduler(this);
  }

  #tempStatusTimer = null;
  #syncTimer = null;

  // Persist the active project. No-op (with a throttled hint) in temp mode.
  save() {
    // The two states that persist nothing: sync off + a fetched server project is edit-in-
    // memory only ("stored nowhere"), and a temporary editor has no project yet. Throttled.
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
    const prev = this.store.getMeta(this.activeId) || {};   // read ONCE: getMeta re-parses the registry
    // The row keeps its last thumbnail; the fresh one is rendered in idle time (thumbnail.js).
    const meta = buildProjectMeta(this.app, { prev, id: this.activeId, layout, thumbnail: prev.thumbnail ?? null });
    upsertWithQuota(this, meta, { image: this.app.imageDataUrl || null, layout });
    this.thumbs.schedule(this.activeId);

    // Tell other tabs this project changed so any tab viewing it re-syncs its
    // editor. Debounced so a burst of edits coalesces into one broadcast.
    this.#scheduleSyncBroadcast();
    // Auto-save the linked .stencil file too, when live file sync is on (debounced, no-op otherwise).
    this.app.stencilSync?.onEdit();
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
    // A crop change keeps the same image but resizes the working canvas, so the light
    // path (lines only) can't represent it — full reload when the stored crop differs.
    // Normalize both sides so a canonical {w,h} stored rect compares equal to the live one.
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

  // The banner itself is a view (ui/imageMissingBanner.js); Storage decides WHEN.
  showImageMissingBanner(show) { paintImageMissingBanner(show); }

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
    this.saveSoon.flush();      // the project we are leaving keeps its last edit…
    this.thumbs.flush();        // …and its thumbnail
    this.activeId = id;
    this.temporary = false;
    this.incognito = false;
    this.app.activeProjectId = id;
    // An OPEN is an image appearing, so it arrives the same way a loaded file does.
    // (The cross-tab sync path below shares this method and stays still: a peer's edit
    // must not dissolve the picture out from under the person reading it.)
    this.loadPayloadIntoApp(proj.payload, { landing: true });
    this.#autoRefreshOnOpen(id);
    return true;
  }

  // When a project has "refresh on open" enabled and an expiration set, opening it
  // restarts its window: expiresAt = now + its refresh period. Keep-forever
  // (expiresAt 0) is left untouched. Broadcasts so other tabs re-render the date.
  #autoRefreshOnOpen(id) {
    const meta = this.store.getMeta(id);
    if (!meta || !meta.autoRefresh || !meta.expiresAt) return;
    const period = meta.refreshPeriod || DEFAULT_PERIOD;
    this.store.setExpiration(id, { expiresAt: addPeriod(Date.now(), period), refreshPeriod: period });
    this.#scheduleSyncBroadcast();
  }

  // Apply a payload {image, layout} into app state + DOM; shared by loadProject()
  // and the migration paths. `landing` plays the arrival once the image is on the
  // canvas — on by an explicit open only (see loadProject).
  loadPayloadIntoApp(payload, { landing = false } = {}) {
    try {
      const layout = (payload && payload.layout) || {};
      const imageDataUrl = (payload && payload.image) || null;
      const targetId = this.activeId; // capture for stale-load guard

      // Restore UI settings. The page size goes through the same validator as the
      // other adoption paths — an unknown stored name keeps the current page rather
      // than poisoning getPageDimensions with an off-table string.
      const pageSize = normalizePageSize(layout.pageSize);
      if (pageSize) {
        this.app.pageSize = pageSize;
        paintPageSize(pageSize);
      }
      if (layout.customPageWidth) this.app.customPageWidth = layout.customPageWidth;
      if (layout.customPageHeight) this.app.customPageHeight = layout.customPageHeight;
      // Restore the display unit, then render the custom page inputs + labels in
      // it (stored values are cm; applyUnitToUI converts for display).
      if (layout.unit) this.app.unit = layout.unit;
      this.app.applyUnitToUI();
      if (layout.color) this.app.color = layout.color;
      // '' is a MEANINGFUL value here ("points follow the line colour"), so this restores
      // on a typeof check rather than truthiness the way the others do.
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
      // The shared sync (settingsController.js), so the toolbar pill's .on class stays
      // in step here too.
      this.app.allowFormulas = layout.allowFormulas !== undefined ? layout.allowFormulas : false;
      this.app.settings.syncFormulaUI(this.app.allowFormulas);
      this.app.formulaX = layout.formulaX || '';
      this.app.formulaY = layout.formulaY || '';
      paintFormulaFields(this.app);
      if (layout.drawMode) this.app.drawMode = layout.drawMode;
      if (Number.isFinite(layout.holdDrawDelay))
        this.app.input.setHoldDrawDelay(layout.holdDrawDelay, { persist: false });   // clamps in one place
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
          this.app.cropRect = normalizeCropRect(layout.cropRect) || this.app.imageModel.defaultCropRect();
          this.app.imageModel.rebuildCroppedImage();
          this.app.lines = layout.lines || [];
          // Empty lines → step -1 (no phantom undo on a brand-new/blank project); only seed
          // a current snapshot when there are real lines to undo back to.
          this.app.history.reset(this.app.lines, this.app.lines.length ? 0 : -1);

          if (layout.zoom) {
            this.app.zoomPan.setZoom(layout.zoom);   // also sizes the viewport (syncViewportHeight)
            // Synchronously, BEFORE the arrival below, so the dust forms over the very
            // slice the user will see (scrollViewportTo says why that reflow matters).
            scrollViewportTo(layout.scrollLeft, layout.scrollTop);
          } else {
            this.app.zoomPan.fitToWindow();
          }

          this.app.updateInfo();
          this.app.renderer.redraw();
          // Same beat as the file loader: the picture is in the backing store, so the
          // arrival goes up in THIS tick or a frame of the finished image flashes first.
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
  // `keepChat` leaves the live conversation alone — the assistant resets the editor
  // MID-TURN (openUrl incognito adoption), and swapping the chat scope under it would
  // wipe the very exchange that asked for the reset.
  newTemporary({ keepChat = false } = {}) {
    this.saveSoon.flush(); this.thumbs.flush();   // …and so does the one being cleared away
    this.activeId = null;
    this.temporary = true;
    this.incognito = false;
    this.app.activeProjectId = null;
    // Chat persistence (§12): a temporary editor has no project to file a chat
    // under — with saving on, the conversation resets to a fresh scope.
    if (!keepChat) this.app.chatPersistence?.projectOpened(null);

    // Was there anything on screen to clear? Boot calls this to start blank, and the
    // dust/hold below would then flash the empty-state card off and back on for no reason.
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
    // Copy the pixels into a throwaway overlay FIRST — clearRect is instant and would
    // leave nothing to animate. The empty state is ALSO held back for the animation,
    // or the blank editor pops in underneath while the dust is still falling.
    // …and ONLY while there really are motes in front of it: ghostOut says so (the same
    // contract as the arrival's ghostIn). Under reduced motion nothing falls, and holding
    // the empty editor back anyway just blanked it for a second for no reason.
    if (ctx && hadImage && ghostOut(this.app.canvas)) {
      const vp = document.getElementById('canvas-viewport');
      if (vp) flashLanding(vp, 'canvas-clearing', GHOST_MS);
    }
    if (ctx) ctx.clearRect(0, 0, this.app.canvas.width, this.app.canvas.height);
    // Collapse the backing store + any inline CSS size left by the last zoom — otherwise the
    // blank canvas keeps its footprint and the idle "+ Blank image" card lands off-centre.
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

  // A temp editor just received its first image → promote to a real project.
  // The caller subsequently calls save() to persist.
  promoteTemporaryToProject() {
    this.activeId = this.store.createId();
    this.temporary = false;
    this.app.activeProjectId = this.activeId;
    return this.activeId;
  }

}
