import { StencilElement, hostTag, define } from './base.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
import { wirePanelResizer } from '../utils.js';
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
    <div id="fs-controls-panel">
        <button id="fs-exit-btn" class="btn-icon-text" data-hk-title="fullscreen" data-title="Exit fullscreen" title="Exit fullscreen">${icon('x', { size: 14 })}<span>Exit</span></button>
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
    <div id="fs-panel-resizer" title="Drag to resize the panel"></div>
    `;
  }
  static template() { return hostTag('stencil-fullscreen-layer', '', StencilFullscreenLayer.inner()); }

  wire(app) {
    const fsControlsPanel = document.getElementById('fs-controls-panel');
    const fsPointsPanel = document.getElementById('fs-points-panel');
    const fsTopTrigger = document.getElementById('fs-top-trigger');
    const fsRightTrigger = document.getElementById('fs-right-trigger');
    const fsExitBtn = document.getElementById('fs-exit-btn');
    const fsBtn = document.getElementById('fullscreen-toggle');

    let isFullscreen = false;
    let controlsHideTimer = null;
    let pointsHideTimer = null;

    // Populate the fullscreen controls panel by cloning the main controls
    const populateFsControls = () => {
      // Clone the controls div into the fs panel (after the exit button)
      const existing = fsControlsPanel.querySelector('.controls');
      if (existing) existing.remove();
      const src = document.querySelector('#controls-body .controls');
      if (src) {
        const clone = src.cloneNode(true);
        fsControlsPanel.appendChild(clone);
        // Note: cloned inputs/buttons are decorative display; interactions remain on originals
        // Make cloned buttons trigger the originals
        bindClonedControls(fsControlsPanel, src);
      }
    };

    const bindClonedControls = (cloneRoot, srcRoot) => {
      // Wire up all interactive elements in the clone to fire events on originals
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

    // Populate the fullscreen points panel by mirroring coordPanel
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
        clone.classList.remove('coord-collapsed');
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
      // Entering fullscreen needs an image to view; block it otherwise (the
      // Alt+F hotkey and context menu route through here too). Exiting is
      // always allowed.
      if (!isFullscreen && !(app && app.image)) return;
      // ── Save zoom & pan BEFORE switching modes ──
      // We record the image-space point at the viewport centre so we can
      // re-centre on the same spot after the viewport geometry changes.
      const vp = document.getElementById('canvas-viewport');
      let savedScale = null;
      let savedImgCx = null;
      let savedImgCy = null;
      if (app && app.image && vp) {
        savedScale = app.scale;
        savedImgCx = (vp.scrollLeft + vp.clientWidth  / 2) / savedScale;
        savedImgCy = (vp.scrollTop  + vp.clientHeight / 2) / savedScale;
      }

      isFullscreen = !isFullscreen;
      document.body.classList.toggle('fullscreen-mode', isFullscreen);
      fsBtn.innerHTML = icon('maximize');
      fsBtn.dataset.title = isFullscreen ? 'Exit fullscreen' : 'Fullscreen mode';
      fsBtn.title = hotkeys.hkTitle(isFullscreen ? 'Exit fullscreen' : 'Fullscreen mode', 'fullscreen');
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
          vp.scrollLeft = Math.max(0, savedImgCx * savedScale - vp.clientWidth  / 2);
          vp.scrollTop = Math.max(0, savedImgCy * savedScale - vp.clientHeight / 2);
        }
      };

      if (isFullscreen) {
        populateFsControls();
        populateFsPoints();
        // Wait one frame so the CSS position:fixed layout is committed and
        // vp.clientWidth/Height reflect the full-window size.
        requestAnimationFrame(() => {
          restoreView();
          // Re-show selection panel if a line is selected
          if (app && app.selectedLineIdx >= 0) app.syncFsSelectionPanel(app.lines[app.selectedLineIdx]);
        });
      } else {
        // Restore normal viewport max-height (CSS position:fixed removed by class toggle).
        // Use the adaptive available height; restoreView()'s refit then hugs+grows it to zoom.
        if (vp) {
          vp.style.maxHeight = (app && app.zoomPan ? app.zoomPan.availContentHeight() : Math.max(300, window.innerHeight - 220)) + 'px';
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
    };
    // Expose on the shared app instance so the hotkey dispatcher and context
    // menu can reach it without a window global.
    app.toggleFullscreen = toggleFullscreen;

    fsBtn.addEventListener('click', () => toggleFullscreen());
    fsExitBtn.addEventListener('click', () => {
      if (isFullscreen) toggleFullscreen();
    });

    document.addEventListener('keydown', e => {
      if (e.key === 'Escape' && isFullscreen) toggleFullscreen();
    });

    // Resize: re-fit in fullscreen (CSS position:fixed handles viewport sizing)
    window.addEventListener('resize', () => {
      if (isFullscreen && app && app.image) app.zoomPan.fitToWindow();
    });
  }
}
define('stencil-fullscreen-layer', StencilFullscreenLayer);
