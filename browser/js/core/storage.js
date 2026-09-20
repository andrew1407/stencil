import { PROJECT_ACTION } from '../worker/messages.js';
import { normalizePageSize } from './units.js';
import { normalizeCropRect } from './layout.js';
import { createTrailingSave } from './zoomPan.js';
import { ghostOut, flashLanding, playCanvasArrival, GHOST_MS } from '../ui/motion.js';
import { showImageMissingBanner as paintImageMissingBanner } from '../ui/imageMissingBanner.js';
import { paintPageSize, paintDrawingControls, paintVisibilityChecks, paintFormulaFields,
         hideSelectionPanels, resetViewportScroll, scrollViewportTo } from '../ui/layoutControls.js';
import { createThumbnailScheduler } from './thumbnail.js';
import { saveBlockedReason, writeActiveProject } from './storageSave.js';
import { attachProjectsStore, restoreProjects, autoRefreshOnOpen,
         promoteTemporary, clearEditorState } from './storageSession.js';
import { applyStoredPage, applyStoredDrawing, applyStoredProvenance, applyStoredFormulas,
         applyStoredTools, applyImagelessPayload } from './storedLayout.js';

// Thin DOM adapter over the DOM-free ProjectsStore for the ACTIVE project. `save()` is a
// no-op in temporary mode.
export class Storage {
  constructor(app) {
    this.app = app;
    this.store = attachProjectsStore(this);
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
    const blocked = saveBlockedReason(this);
    if (blocked) {
      if (!this.#tempStatusTimer) {
        this.app.showSaveStatus(blocked, 'var(--warning)', 'info');
        this.#tempStatusTimer = setTimeout(() => { this.#tempStatusTimer = null; }, 1500);
      }
      return;
    }

    writeActiveProject(this);

    // Debounced so a burst of edits coalesces into one cross-tab broadcast.
    this.scheduleSyncBroadcast();
    this.app.stencilSync?.onEdit();
  }

  scheduleSyncBroadcast() {
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

  restore() { restoreProjects(this); }

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
    autoRefreshOnOpen(this, id);
    return true;
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
      applyStoredPage(this.app, layout);
      applyStoredDrawing(this.app, layout);
      paintDrawingControls(this.app, layout);
      applyStoredProvenance(this.app, layout);
      applyStoredFormulas(this.app, layout);
      paintFormulaFields(this.app);
      applyStoredTools(this.app, layout);

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
      } else {
        applyImagelessPayload(this, layout);
      }
    } catch (e) {
      console.warn('Could not load project payload:', e);
    }
  }

  // `keepChat` is for the assistant resetting the editor MID-TURN: swapping the chat scope
  // would wipe the exchange that asked for the reset.
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

    clearEditorState(this.app);

    const ctx = this.app.ctx;
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

  promoteTemporaryToProject() { return promoteTemporary(this); }

}
