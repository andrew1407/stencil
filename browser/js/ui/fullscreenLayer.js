import { StencilElement, hostTag, define } from './base.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
import { wirePanelResizer, onWindowResize } from '../utils.js';
import { flipFrom, FLIP_MS } from './motion.js';
import { canvasOrigin } from '../core/zoomPan.js';
import { publish, EVENTS } from '../bus/appBus.js';
import { fullscreenLayerInner } from './fullscreenMarkup.js';
import { populateFsControls, populateFsPoints } from './fullscreenClones.js';
import { createFsPanels } from './fullscreenPanels.js';
// ── Component: fullscreen trigger zones + slide-in panels ───────
// Owns the fs trigger/panel markup and fullscreen behavior (cloning the live
// controls + coord panel, slide-in panels, enter/exit). Exposes the toggle as
// app.toggleFullscreen, which context menu / hotkeys call through their own app
// reference — no window global.
export class StencilFullscreenLayer extends StencilElement {
  static inner() { return fullscreenLayerInner(); }
  static template() { return hostTag('stencil-fullscreen-layer', '', StencilFullscreenLayer.inner()); }

  wire(app) {
    const fsControlsPanel = document.getElementById('fs-controls-panel');
    const fsPointsPanel = document.getElementById('fs-points-panel');
    const fsTopTrigger = document.getElementById('fs-top-trigger');
    const fsRightTrigger = document.getElementById('fs-right-trigger');
    const fsBtn = document.getElementById('fullscreen-toggle');

    let isFullscreen = false;

    // The controls strip / coord panel clones (ui/fullscreenClones.js) and the two
    // slide-in panels with their auto-hide timers (ui/fullscreenPanels.js).
    const showPoints = () => populateFsPoints(fsPointsPanel);
    const {
      showControlsPanel, hideControlsPanel, showPointsPanel, hidePointsPanel,
      pauseControlsHide, pausePointsHide,
    } = createFsPanels({ fsControlsPanel, fsPointsPanel, showPoints });

    // Keep fs points panel in sync when the coord table updates. The coord
    // table mutates per-frame while drawing, so coalesce bursts into a single
    // rebuild per animation frame instead of re-cloning the panel each mutation.
    const coordBody = document.getElementById('coordinates-body');
    if (coordBody) {
      let fsPointsRaf = 0;
      new MutationObserver(() => {
        if (!isFullscreen || fsPointsRaf) return;
        fsPointsRaf = requestAnimationFrame(() => {
          fsPointsRaf = 0;
          if (isFullscreen) showPoints();
        });
      }).observe(coordBody, { childList: true, subtree: true, characterData: true });
    }


    // Trigger zone hover
    fsTopTrigger.addEventListener('mouseenter', showControlsPanel);
    fsTopTrigger.addEventListener('mouseleave', hideControlsPanel);
    fsControlsPanel.addEventListener('mouseenter', pauseControlsHide);
    fsControlsPanel.addEventListener('mouseleave', hideControlsPanel);

    fsRightTrigger.addEventListener('mouseenter', showPointsPanel);
    fsRightTrigger.addEventListener('mouseleave', hidePointsPanel);
    fsPointsPanel.addEventListener('mouseenter', pausePointsHide);
    fsPointsPanel.addEventListener('mouseleave', hidePointsPanel);

    // ── Fullscreen panel resizer: drag to set --coord-panel-width (shared with normal mode +
    // persisted). The panel is on the RIGHT, so dragging the handle LEFT widens it. The hooks
    // pause the panel's auto-hide during a drag. ──
    const fsResizer = document.getElementById('fs-panel-resizer');
    if (fsResizer) {
      const pauseAutoHide = pausePointsHide;
      const drag = wirePanelResizer(fsResizer, fsPointsPanel, {
        maxFactor: 0.86, onStart: pauseAutoHide, onEnd: pauseAutoHide,
      });
      // The resizer sits just OUTSIDE the panel's left edge, so moving onto it fires the panel's
      // mouseleave (→ auto-hide). Treat it as part of the panel's hover zone so the panel (and thus
      // the resizer) stays put while you reach for / drag it.
      fsResizer.addEventListener('mouseenter', pauseAutoHide);
      fsResizer.addEventListener('mouseleave', () => { if (!drag.isDragging()) hidePointsPanel(); });
    }

    // ── Enter / Exit fullscreen ──
    const toggleFullscreen = () => {
      // Fullscreen is available with or without an image: the empty canvas carries
      // the "＋ Blank image" invitation and the whole toolbar, so entering it on an
      // imageless editor is a perfectly good way to start work with more room.
      // ── Save zoom & pan BEFORE switching modes ──
      // We record the image-space point at the viewport centre so we can
      // re-centre on the same spot after the viewport geometry changes.
      const vp = document.getElementById('canvas-viewport');
      let savedScale = null;
      let savedImgCx = null;
      let savedImgCy = null;
      if (app && app.image && vp) {
        savedScale = app.scale;
        // Minus the centring margins (canvasOrigin): a fitted picture sits in the middle
        // of the frame, not at the scroll origin, so without them the "centre" saved here
        // is half a viewport off and fullscreen opens looking somewhere else.
        const org = canvasOrigin();
        savedImgCx = (vp.scrollLeft + vp.clientWidth  / 2 - org.x) / savedScale;
        savedImgCy = (vp.scrollTop  + vp.clientHeight / 2 - org.y) / savedScale;
      }

      // The box the viewport occupies RIGHT NOW — replayed as the start of the
      // stretch (entering) or the end of the minimise (leaving) once the mode has
      // switched and the new box is laid out.
      const flipFromRect = vp ? vp.getBoundingClientRect() : null;

      isFullscreen = !isFullscreen;
      document.body.classList.toggle('fullscreen-mode', isFullscreen);
      // Components pinned to a toolbar icon need to know: the toolbar they measured is
      // about to be hidden and re-cloned elsewhere (the chat panel's popover).
      try {
        publish(EVENTS.fullscreenChanged, { on: isFullscreen });
      } catch { /* no DOM (tests) */ }
      // The glyph turns over with the state, and each one's hover moves the way the click
      // will (iconMotion.json maximize / minimize).
      fsBtn.innerHTML = icon(isFullscreen ? 'minimize' : 'maximize');
      fsBtn.dataset.title = isFullscreen ? 'Exit fullscreen' : 'Fullscreen mode';
      fsBtn.dataset.tip = hotkeys.hkTitle(isFullscreen ? 'Exit fullscreen' : 'Fullscreen mode', 'fullscreen');
      // Accent-fill only while fullscreen is active (via the shared .active ghost-button style),
      // so the button reads flat like the rest of the Settings row when not in fullscreen.
      fsBtn.classList.toggle('active', isFullscreen);

      // Helper: restore the saved zoom level and re-centre the viewport on the
      // same image-space point.  Falls back to fitToWindow if no state was saved.
      const restoreView = () => {
        if (!app || !app.image) return;
        if (savedScale === null) { app.zoomPan.fitToWindow(); return; }
        app.zoomPan.setZoom(savedScale, true);
        if (vp && savedImgCx !== null) {
          // Put the centring margin back on — the saved centre had it taken off. It is 0
          // once the picture overflows, so this only matters while it fits.
          const org = app.zoomPan.originAt(savedScale);
          vp.scrollLeft = Math.max(0, savedImgCx * savedScale + org.x - vp.clientWidth  / 2);
          vp.scrollTop = Math.max(0, savedImgCy * savedScale + org.y - vp.clientHeight / 2);
        }
      };

      if (isFullscreen) {
        // Hand the box over to the fullscreen rule (components/fullscreen.css pins it to the window):
        // the in-flow height written by syncViewportHeight has no business here.
        if (vp) vp.style.maxHeight = '';
        populateFsControls(fsControlsPanel);
        showPoints();
        // Wait one frame so the CSS position:fixed layout is committed and
        // vp.clientWidth/Height reflect the full-window size.
        requestAnimationFrame(() => {
          restoreView();
          if (app && app.selectedLineIdx >= 0) app.syncFsSelectionPanel(app.lines[app.selectedLineIdx]);
        });
      } else {
        // Back in flow (CSS position:fixed removed by the class toggle) — restore the
        // full-height frame the normal mode's rule gives it.
        if (vp) {
          if (app && app.zoomPan) {
            app.zoomPan.syncViewportHeight();
            // …and again once the exit flight has landed: measured now, the toolbar rows are
            // still coming back and the viewport's top is ~60px off, which would leave the
            // frame too tall (a permanent page scrollbar).
            setTimeout(() => app.zoomPan.syncViewportHeight(), FLIP_MS + 120);
          } else {
            vp.style.maxHeight = Math.max(300, window.innerHeight - 220) + 'px';
          }
          vp.style.maxWidth = '';
        }
        fsControlsPanel.classList.remove('fs-panel-visible');
        fsPointsPanel.classList.remove('fs-panel-visible');
        const fsSel = document.getElementById('fs-selection-panel');
        if (fsSel) fsSel.style.display = 'none';
        const trigger = document.getElementById('fs-top-trigger');
        if (trigger) trigger.style.height = '8px';
        restoreView();
      }
      // Measured last, after both branches have settled the viewport's box (the exit
      // path writes maxHeight, which moves it). Transform-only — nothing above re-runs.
      flipFrom(vp, flipFromRect);
    };
    // Expose on the shared app instance so the hotkey dispatcher and context
    // menu can reach it without a window global.
    app.toggleFullscreen = toggleFullscreen;

    fsBtn.addEventListener('click', () => toggleFullscreen());

    document.addEventListener('keydown', e => {
      if (e.key === 'Escape' && isFullscreen) toggleFullscreen();
    });

    // Resize: re-fit in fullscreen (CSS position:fixed handles viewport sizing)
    onWindowResize(() => { if (isFullscreen && app && app.image) app.zoomPan.fitToWindow(); });
  }
}
define('stencil-fullscreen-layer', StencilFullscreenLayer);
