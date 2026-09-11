import { StencilElement, hostTag, define } from './base.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
import { wirePanelResizer, onWindowResize } from '../utils.js';
import { flipFrom, FLIP_MS } from './motion.js';
import { canvasOrigin } from '../core/zoomPan.js';
import { publish, EVENTS } from '../bus/appBus.js';
// ── Component: fullscreen trigger zones + slide-in panels ───────
// Owns the fs trigger/panel markup and fullscreen behavior (cloning the live
// controls + coord panel, slide-in panels, enter/exit). Exposes the toggle as
// app.toggleFullscreen, which context menu / hotkeys call through their own app
// reference — no window global.
export class StencilFullscreenLayer extends StencilElement {
  static inner() {
    return `
    <!-- Fullscreen hover trigger zones -->
    <div id="fs-top-trigger"></div>
    <div id="fs-right-trigger"></div>

    <!-- Fullscreen slide-in: controls (top) -->
    <!-- No Exit button of its own: the revealed strip IS the toolbar, and the fullscreen
         toggle inside it (lit while on) is what leaves — plus Escape. A second control for
         the same thing sat over the cloned rows and read as part of them (user decision). -->
    <div id="fs-controls-panel">
        <!-- Controls content will be cloned here by JS -->
    </div>

    <!-- Fullscreen selection panel overlay (shown over canvas when a line is selected) -->
    <div id="fs-selection-panel" style="display:none;position:fixed;z-index:10001;left:0;right:0;pointer-events:auto;"></div>

    <!-- Fullscreen slide-in: points list (right) -->
    <div id="fs-points-panel">
        <!-- Coord panel content will be mirrored here by JS -->
    </div>
    <!-- Drag handle to resize the fullscreen points panel (sibling of the panel so the panel's
         innerHTML re-clone doesn't wipe it). Positioned at the panel's left edge via the width var. -->
    <div id="fs-panel-resizer"></div>
    `;
  }
  static template() { return hostTag('stencil-fullscreen-layer', '', StencilFullscreenLayer.inner()); }

  wire(app) {
    const fsControlsPanel = document.getElementById('fs-controls-panel');
    const fsPointsPanel = document.getElementById('fs-points-panel');
    const fsTopTrigger = document.getElementById('fs-top-trigger');
    const fsRightTrigger = document.getElementById('fs-right-trigger');
    const fsBtn = document.getElementById('fullscreen-toggle');

    let isFullscreen = false;
    let controlsHideTimer = null;
    let pointsHideTimer = null;

    const populateFsControls = () => {
      const existing = fsControlsPanel.querySelector('.controls');
      if (existing) existing.remove();
      const src = document.querySelector('#controls-body .controls');
      if (src) {
        const clone = src.cloneNode(true);
        fsControlsPanel.appendChild(clone);
        bindClonedControls(fsControlsPanel, src);
      }
    };

    // The clone is display only — every interaction relays to the original controls.
    const bindClonedControls = (cloneRoot, srcRoot) => {
      srcRoot.querySelectorAll('[id]').forEach(srcEl => {
        const cloneEl = cloneRoot.querySelector('#' + srcEl.id);
        if (!cloneEl) return;
        if (cloneEl.tagName === 'BUTTON') {
          cloneEl.addEventListener('click', () => srcEl.click());
          return;
        }
        // Mirror only the input kinds the fs panel uses; ignore others (radio/text).
        const kind = cloneEl.tagName === 'SELECT' ? 'select' : cloneEl.type;
        if (!['checkbox', 'color', 'number', 'file', 'select'].includes(kind)) return;

        // Relay clone → original: copy the relevant property, then fire change.
        cloneEl.addEventListener(kind === 'color' ? 'input' : 'change', () => {
          if (kind === 'checkbox') {
            srcEl.checked = cloneEl.checked;
          } else if (kind === 'file') {
            const dt = new DataTransfer();
            [...cloneEl.files].forEach(f => dt.items.add(f));
            srcEl.files = dt.files;
          } else {
            srcEl.value = cloneEl.value;
          }
          srcEl.dispatchEvent(new Event('change', { bubbles: true }));
        });
        // Keep the clone in sync when the original changes (checkbox + select).
        if (kind === 'checkbox') srcEl.addEventListener('change', () => { cloneEl.checked = srcEl.checked; });
        else if (kind === 'select') srcEl.addEventListener('change', () => { cloneEl.value = srcEl.value; });
      });
    };

    const populateFsPoints = () => {
      fsPointsPanel.innerHTML = '';
      const src = document.getElementById('coord-panel');
      if (src) {
        const clone = src.cloneNode(true);
        // Give cloned elements new ids to avoid conflicts
        clone.id = 'fs-coord-panel-clone';
        clone.querySelectorAll('[id]').forEach(el => {
          el.id = 'fs-clone-' + el.id;
        });
        clone.classList.remove('coord-collapsed', 'coord-folding');
        clone.style.minWidth = '0';
        clone.style.maxWidth = '100%';
        clone.style.marginTop = '0';
        clone.style.background = 'transparent';
        fsPointsPanel.appendChild(clone);
      }
    };

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
          if (isFullscreen) populateFsPoints();
        });
      }).observe(coordBody, { childList: true, subtree: true, characterData: true });
    }

    // ── Panel show/hide helpers ──
    const updateFsSelectionTop = ctrlsVisible => {
      const fsSel = document.getElementById('fs-selection-panel');
      if (!fsSel || fsSel.style.display === 'none') return;
      if (ctrlsVisible) {
        // Use offsetHeight — not getBoundingClientRect — so transform doesn't affect measurement
        fsSel.style.top = fsControlsPanel.offsetHeight + 'px';
      } else {
        fsSel.style.top = '0px';
      }
      // Keep trigger zone covering the selection panel
      requestAnimationFrame(() => {
        const trigger = document.getElementById('fs-top-trigger');
        if (trigger) trigger.style.height = Math.max(8, fsSel.getBoundingClientRect().bottom) + 'px';
      });
    };

    const showControlsPanel = () => {
      clearTimeout(controlsHideTimer);
      // Update selection panel top BEFORE adding class so CSS transitions start together
      updateFsSelectionTop(true);
      fsControlsPanel.classList.add('fs-panel-visible');
    };
    const hideControlsPanel = () => {
      clearTimeout(controlsHideTimer);
      controlsHideTimer = setTimeout(() => {
        // Update selection panel top BEFORE removing class
        updateFsSelectionTop(false);
        fsControlsPanel.classList.remove('fs-panel-visible');
        // After CSS transition (0.25s), update trigger height
        setTimeout(() => {
          const fsSel = document.getElementById('fs-selection-panel');
          const trigger = document.getElementById('fs-top-trigger');
          if (fsSel && trigger && fsSel.style.display !== 'none')
            trigger.style.height = Math.max(8, fsSel.getBoundingClientRect().bottom) + 'px';
        }, 260);
      }, 400);
    };
    const showPointsPanel = () => {
      clearTimeout(pointsHideTimer);
      populateFsPoints();
      fsPointsPanel.classList.add('fs-panel-visible');
    };
    const hidePointsPanel = () => {
      clearTimeout(pointsHideTimer);
      pointsHideTimer = setTimeout(() => {
        fsPointsPanel.classList.remove('fs-panel-visible');
      }, 400);
    };

    // Trigger zone hover
    fsTopTrigger.addEventListener('mouseenter', showControlsPanel);
    fsTopTrigger.addEventListener('mouseleave', hideControlsPanel);
    fsControlsPanel.addEventListener('mouseenter', () => { clearTimeout(controlsHideTimer); });
    fsControlsPanel.addEventListener('mouseleave', hideControlsPanel);

    fsRightTrigger.addEventListener('mouseenter', showPointsPanel);
    fsRightTrigger.addEventListener('mouseleave', hidePointsPanel);
    fsPointsPanel.addEventListener('mouseenter', () => { clearTimeout(pointsHideTimer); });
    fsPointsPanel.addEventListener('mouseleave', hidePointsPanel);

    // ── Fullscreen panel resizer: drag to set --coord-panel-width (shared with normal mode +
    // persisted). The panel is on the RIGHT, so dragging the handle LEFT widens it. The hooks
    // pause the panel's auto-hide during a drag. ──
    const fsResizer = document.getElementById('fs-panel-resizer');
    if (fsResizer) {
      const pauseAutoHide = () => clearTimeout(pointsHideTimer);
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
        // Hand the box over to the fullscreen rule (components.css pins it to the window):
        // the in-flow height written by syncViewportHeight has no business here.
        if (vp) vp.style.maxHeight = '';
        populateFsControls();
        populateFsPoints();
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
