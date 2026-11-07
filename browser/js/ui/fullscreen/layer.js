import { StencilElement, hostTag, define } from '../base.js';
import { wirePanelResizer, onWindowResize, notify } from '../../utils.js';
import { motionMs } from '../motion/motionPrefs.js';
import { glideFrom, FLIP_MS } from '../motion/flip.js';
import { canvasOrigin } from '../../core/zoom/pan.js';
import { publish, EVENTS } from '../../eventBus/appBus.js';
import { fullscreenLayerInner } from './markup.js';
import { populateFsControls, clearFsControls, populateFsPoints, followCoordBody } from './clones.js';
import { createFsPanels } from './panels.js';
import { syncFsSelectionPanel } from '../panel/selectionPanel.js';
import { windowTookEscape } from '../modal/registry.js';
import { afterIconDrag } from '../drag/iconDrag.js';
import { wireCloneDrags } from '../bindings/controls/toolbarDrags.js';
import { wireFullscreenButton, syncFsButton } from './button.js';
import { enterScreen, exitScreen, isScreenFull, onScreenChange, screenRefusal } from './screen.js';

// How long after an OS switch its resize keeps the zoom rather than refitting (ms).
const SCREEN_SETTLE_MS = 1200;
// Looked up per use, as the frame may be re-mounted.
const viewport = () => document.getElementById('canvas-viewport');

// A drag out of a panel keeps it up until the drop; it then goes unless the pointer came back.
const leftUnlessDragging = (panel, hide) => () =>
  afterIconDrag(() => { if (!panel.matches?.(':hover')) hide(); });

// The image-space point at the viewport centre, and a restore that re-centres on it at the
// saved zoom once the geometry changed (fitToWindow when nothing was saved).
const captureView = (app, vp) => {
  let savedScale = null, cx = null, cy = null;
  if (app && app.image && vp) {
    savedScale = app.scale;
    // Minus the centring margins (canvasOrigin): a fitted picture sits mid-frame, not at the scroll origin.
    const org = canvasOrigin();
    cx = (vp.scrollLeft + vp.clientWidth  / 2 - org.x) / savedScale;
    cy = (vp.scrollTop  + vp.clientHeight / 2 - org.y) / savedScale;
  }
  return () => {
    if (!app || !app.image) return;
    if (savedScale === null) { app.zoomPan.fitToWindow(); return; }
    app.zoomPan.setZoom(savedScale, true);
    if (vp && cx !== null) {
      // The centring margin goes back on; it is 0 once the picture overflows.
      const org = app.zoomPan.originAt(savedScale);
      vp.scrollLeft = Math.max(0, cx * savedScale + org.x - vp.clientWidth  / 2);
      vp.scrollTop = Math.max(0, cy * savedScale + org.y - vp.clientHeight / 2);
    }
  };
};

// The hover bands that slide each panel in, and the points panel's resize handle.
const wireTriggerZones = ({ fsTopTrigger, fsRightTrigger, fsControlsPanel, fsPointsPanel }, {
  showControlsPanel, hideControlsPanel, showPointsPanel, hidePointsPanel, pauseControlsHide, pausePointsHide,
}) => {
  fsTopTrigger.addEventListener('mouseenter', showControlsPanel);
  fsTopTrigger.addEventListener('mouseleave', hideControlsPanel);
  fsControlsPanel.addEventListener('mouseenter', pauseControlsHide);
  fsControlsPanel.addEventListener('mouseleave', leftUnlessDragging(fsControlsPanel, hideControlsPanel));

  fsRightTrigger.addEventListener('mouseenter', showPointsPanel);
  fsRightTrigger.addEventListener('mouseleave', hidePointsPanel);
  fsPointsPanel.addEventListener('mouseenter', pausePointsHide);
  fsPointsPanel.addEventListener('mouseleave', leftUnlessDragging(fsPointsPanel, hidePointsPanel));

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
};

// Fullscreen trigger zones + slide-in panels; exposes the toggle as app.toggleFullscreen(kind)
// and the live kind as app.fullscreenKind() (no window global).
export class StencilFullscreenLayer extends StencilElement {
  static inner() { return fullscreenLayerInner(); }
  static template() { return hostTag('stencil-fullscreen-layer', '', StencilFullscreenLayer.inner()); }

  wire(app) {
    const fsControlsPanel = document.getElementById('fs-controls-panel');
    const fsPointsPanel = document.getElementById('fs-points-panel');
    const fsTopTrigger = document.getElementById('fs-top-trigger');
    const fsRightTrigger = document.getElementById('fs-right-trigger');
    const fsBtn = document.getElementById('fullscreen-toggle');

    let mode = null;   // null | 'screen' | 'window' (button.js FS_KINDS)

    const showPoints = () => populateFsPoints(fsPointsPanel);
    const {
      showControlsPanel, hideControlsPanel, showPointsPanel, hidePointsPanel,
      pauseControlsHide, pausePointsHide, reset: resetFsPanels,
    } = createFsPanels({ fsControlsPanel, fsPointsPanel, showPoints });

    followCoordBody(document.getElementById('coord-body'), () => !!mode, showPoints);

    wireTriggerZones({ fsTopTrigger, fsRightTrigger, fsControlsPanel, fsPointsPanel }, {
      showControlsPanel, hideControlsPanel, showPointsPanel, hidePointsPanel, pauseControlsHide, pausePointsHide,
    });

    // The in-app layer on (a kind) or off (null); the OS half is choose()'s.
    const setLayer = (next) => {
// Works without an image too.
      const vp = viewport();
      const restoreView = captureView(app, vp);
// The viewport's box right now: the start of the stretch (entering) or the end of the
// minimise (leaving).
      const flipFromRect = vp ? vp.getBoundingClientRect() : null;

      mode = next;
      document.body.classList.toggle('fullscreen-mode', !!mode);
// The toolbar a pinned popover measured is about to be hidden and re-cloned elsewhere.
      try {
        publish(EVENTS.fullscreenChanged, { on: !!mode });
      } catch { /* no DOM (tests) */ }
// Each glyph's hover moves the way the click will (iconMotion.json maximize / minimize).
      syncFsButton(fsBtn, mode);

      if (mode) {
// components/fullscreen.css pins the box to the window; syncViewportHeight's in-flow height must go.
        if (vp) vp.style.maxHeight = '';
        populateFsControls(fsControlsPanel);
        wireCloneDrags(app, fsControlsPanel);
        fsMenu?.wireCopyIn(fsControlsPanel);
        showPoints();
// One frame, so position:fixed is committed and vp.clientWidth/Height are full-window.
        requestAnimationFrame(() => {
          restoreView();
          if (app?.selectedLineIdx >= 0) syncFsSelectionPanel(app, app.lines[app.selectedLineIdx]);
        });
      } else {
// Back in flow: restore the full-height frame the normal mode's rule gives it.
        if (vp) {
          if (app && app.zoomPan) {
            app.zoomPan.syncViewportHeight();
// …and again once the exit flight has landed: the toolbar rows are still coming back now,
// and the early measure leaves the frame too tall (a permanent page scrollbar).
            setTimeout(() => app.zoomPan.syncViewportHeight(), motionMs(FLIP_MS + 120));
          } else {
            vp.style.maxHeight = Math.max(300, window.innerHeight - 220) + 'px';
          }
          vp.style.maxWidth = '';
        }
        const fsSel = document.getElementById('fs-selection-panel');
        if (fsSel) fsSel.style.display = 'none';
        resetFsPanels();   // after the overlay is gone, so the band it measures is the bare one
        clearFsControls(fsControlsPanel);
        app?.updateButtons?.();   // the gates fed the copy while it held the ids
        restoreView();
      }
// Measured last, after both branches settled the viewport's box. Transform-only.
      glideFrom(vp, flipFromRect);
    };

    // The OS switch resizes the window after the layer settled: hold the zoom across it, as the
    // desktop's beginFullscreenZoom does, instead of the resize handler's refit.
    let screenRestore = null, screenAt = -Infinity;
    const holdViewAcrossScreen = () => {
      screenRestore = captureView(app, viewport());
      screenAt = Date.now();
    };
    const leave = () => {
      if (!mode) return;
      if (isScreenFull()) exitScreen();
      setLayer(null);
    };
    // No kind: off when on, else the OS fullscreen. A kind: off when it is the live one, else into it.
    const choose = (kind) => {
      if (mode && (kind == null || kind === mode)) { leave(); return; }
      const want = kind || 'screen';
      if (mode) { mode = want; syncFsButton(fsBtn, mode); } else setLayer(want);
      if (want === 'window') { if (isScreenFull()) { holdViewAcrossScreen(); exitScreen(); } return; }
      holdViewAcrossScreen();
      enterScreen().then((ok) => {
        if (ok || mode !== 'screen') return;
        mode = 'window';
        syncFsButton(fsBtn, mode);
        // Said, not silent: the in-window kind stands in only because the browser refused the screen.
        notify(`Full screen was refused (${screenRefusal()}) — showing it in the window`, 'info');
      });
    };
    app.toggleFullscreen = choose;
    app.fullscreenKind = () => mode;
    const fsMenu = wireFullscreenButton(fsBtn, { getMode: () => mode, choose });
    syncFsButton(fsBtn, mode);
    // The browser's own way out (Escape, F11, the system UI) leaves the layer with it.
    onScreenChange((full) => { if (!full && mode === 'screen') leave(); });

    document.addEventListener('keydown', e => {
      // An open window owns Escape first, whichever listener runs first.
      if (e.key !== 'Escape' || !mode || windowTookEscape(e) || document.querySelector('.modal-open')) return;
      leave();
    });

    onWindowResize(() => {
      if (!mode || !app || !app.image) return;
      if (screenRestore && Date.now() - screenAt < SCREEN_SETTLE_MS) { screenRestore(); return; }
      app.zoomPan.fitToWindow();
    });
  }
}
define('stencil-fullscreen-layer', StencilFullscreenLayer);
