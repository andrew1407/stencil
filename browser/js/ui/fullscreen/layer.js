import { StencilElement, hostTag, define } from '../base.js';
import { hotkeys } from '../../core/settings/hotkeys.js';
import { icon } from '../icons.js';
import { wirePanelResizer, onWindowResize } from '../../utils.js';
import { flipFrom, FLIP_MS } from '../motion.js';
import { canvasOrigin } from '../../core/zoom/pan.js';
import { publish, EVENTS } from '../../eventBus/appBus.js';
import { fullscreenLayerInner } from './markup.js';
import { populateFsControls, populateFsPoints } from './clones.js';
import { createFsPanels } from './panels.js';
// Fullscreen trigger zones + slide-in panels; exposes the toggle as app.toggleFullscreen
// (no window global).
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

    const showPoints = () => populateFsPoints(fsPointsPanel);
    const {
      showControlsPanel, hideControlsPanel, showPointsPanel, hidePointsPanel,
      pauseControlsHide, pausePointsHide, reset: resetFsPanels,
    } = createFsPanels({ fsControlsPanel, fsPointsPanel, showPoints });

// The coord table mutates per frame while drawing: coalesce into one rebuild per frame.
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

// Drag sets --coord-panel-width (shared with normal mode for this page's life); the panel is
// on the right, so dragging left widens it.
    const fsResizer = document.getElementById('fs-panel-resizer');
    if (fsResizer) {
      const pauseAutoHide = pausePointsHide;
      const drag = wirePanelResizer(fsResizer, fsPointsPanel, {
        maxFactor: 0.86, onStart: pauseAutoHide, onEnd: pauseAutoHide,
      });
// The resizer sits just outside the panel's left edge, so moving onto it fires the panel's
// mouseleave; treat it as part of the hover zone.
      fsResizer.addEventListener('mouseenter', pauseAutoHide);
      fsResizer.addEventListener('mouseleave', () => { if (!drag.isDragging()) hidePointsPanel(); });
    }

    const toggleFullscreen = () => {
// Works without an image too. The image-space point at the viewport centre is saved so the
// view re-centres on it after the geometry changes.
      const vp = document.getElementById('canvas-viewport');
      let savedScale = null;
      let savedImgCx = null;
      let savedImgCy = null;
      if (app && app.image && vp) {
        savedScale = app.scale;
// Minus the centring margins (canvasOrigin): a fitted picture sits mid-frame, not at the
// scroll origin.
        const org = canvasOrigin();
        savedImgCx = (vp.scrollLeft + vp.clientWidth  / 2 - org.x) / savedScale;
        savedImgCy = (vp.scrollTop  + vp.clientHeight / 2 - org.y) / savedScale;
      }

// The viewport's box right now: the start of the stretch (entering) or the end of the
// minimise (leaving).
      const flipFromRect = vp ? vp.getBoundingClientRect() : null;

      isFullscreen = !isFullscreen;
      document.body.classList.toggle('fullscreen-mode', isFullscreen);
// The toolbar a pinned popover measured is about to be hidden and re-cloned elsewhere.
      try {
        publish(EVENTS.fullscreenChanged, { on: isFullscreen });
      } catch { /* no DOM (tests) */ }
// Each glyph's hover moves the way the click will (iconMotion.json maximize / minimize).
      fsBtn.innerHTML = icon(isFullscreen ? 'minimize' : 'maximize');
      fsBtn.dataset.title = isFullscreen ? 'Exit fullscreen' : 'Fullscreen mode';
      fsBtn.dataset.tip = hotkeys.hkTitle(isFullscreen ? 'Exit fullscreen' : 'Fullscreen mode', 'fullscreen');
// Accent-fill only while active (the shared .active ghost-button style).
      fsBtn.classList.toggle('active', isFullscreen);

// Restore the saved zoom and re-centre on the saved image-space point; fitToWindow otherwise.
      const restoreView = () => {
        if (!app || !app.image) return;
        if (savedScale === null) { app.zoomPan.fitToWindow(); return; }
        app.zoomPan.setZoom(savedScale, true);
        if (vp && savedImgCx !== null) {
// The centring margin goes back on; it is 0 once the picture overflows.
          const org = app.zoomPan.originAt(savedScale);
          vp.scrollLeft = Math.max(0, savedImgCx * savedScale + org.x - vp.clientWidth  / 2);
          vp.scrollTop = Math.max(0, savedImgCy * savedScale + org.y - vp.clientHeight / 2);
        }
      };

      if (isFullscreen) {
// components/fullscreen.css pins the box to the window; syncViewportHeight's in-flow height must go.
        if (vp) vp.style.maxHeight = '';
        populateFsControls(fsControlsPanel);
        showPoints();
// One frame, so position:fixed is committed and vp.clientWidth/Height are full-window.
        requestAnimationFrame(() => {
          restoreView();
          if (app && app.selectedLineIdx >= 0) app.syncFsSelectionPanel(app.lines[app.selectedLineIdx]);
        });
      } else {
// Back in flow: restore the full-height frame the normal mode's rule gives it.
        if (vp) {
          if (app && app.zoomPan) {
            app.zoomPan.syncViewportHeight();
// …and again once the exit flight has landed: the toolbar rows are still coming back now,
// and the early measure leaves the frame too tall (a permanent page scrollbar).
            setTimeout(() => app.zoomPan.syncViewportHeight(), FLIP_MS + 120);
          } else {
            vp.style.maxHeight = Math.max(300, window.innerHeight - 220) + 'px';
          }
          vp.style.maxWidth = '';
        }
        const fsSel = document.getElementById('fs-selection-panel');
        if (fsSel) fsSel.style.display = 'none';
        resetFsPanels();   // after the overlay is gone, so the band it measures is the bare one
        restoreView();
      }
// Measured last, after both branches settled the viewport's box. Transform-only.
      flipFrom(vp, flipFromRect);
    };
    app.toggleFullscreen = toggleFullscreen;

    fsBtn.addEventListener('click', () => toggleFullscreen());

    document.addEventListener('keydown', e => {
      if (e.key === 'Escape' && isFullscreen) toggleFullscreen();
    });

    onWindowResize(() => { if (isFullscreen && app && app.image) app.zoomPan.fitToWindow(); });
  }
}
define('stencil-fullscreen-layer', StencilFullscreenLayer);
