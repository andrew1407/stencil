import { StencilElement, hostTag, define } from './base.js';
// ── Component: fullscreen trigger zones + slide-in panels ───────
// Owns the fs trigger/panel markup and the fullscreen behavior (cloning the
// live controls + coord panel, slide-in panels, enter/exit). Exposes the toggle
// on the app instance as app.toggleFullscreen, which other modules (context
// menu, hotkeys) call through their own app reference — no window global.
export class StencilFullscreenLayer extends StencilElement {
  static inner() {
    return `
    <!-- Fullscreen hover trigger zones -->
    <div id="fs-top-trigger"></div>
    <div id="fs-right-trigger"></div>

    <!-- Fullscreen slide-in: controls (top) -->
    <div id="fs-controls-panel">
        <button id="fs-exit-btn" title="Exit fullscreen (Alt+F)">✕ Exit</button>
        <!-- Controls content will be cloned here by JS -->
    </div>

    <!-- Fullscreen selection panel overlay (shown over canvas when a line is selected) -->
    <div id="fs-selection-panel" style="display:none;position:fixed;z-index:10001;left:0;right:0;pointer-events:auto;"></div>

    <!-- Fullscreen slide-in: points list (right) -->
    <div id="fs-points-panel">
        <!-- Coord panel content will be mirrored here by JS -->
    </div>
    `;
  }
  static template() { return hostTag('stencil-fullscreen-layer', '', StencilFullscreenLayer.inner()); }

  wire(app) {
    const fsControlsPanel = document.getElementById('fs-controls-panel');
    const fsPointsPanel = document.getElementById('fs-points-panel');
    const fsTopTrigger = document.getElementById('fs-top-trigger');
    const fsRightTrigger = document.getElementById('fs-right-trigger');
    const fsExitBtn = document.getElementById('fs-exit-btn');
    const fsBtn = document.getElementById('fullscreenToggle');

    let isFullscreen = false;
    let controlsHideTimer = null;
    let pointsHideTimer = null;

    // Populate the fullscreen controls panel by cloning the main controls
    const populateFsControls = () => {
      // Clone the controls div into the fs panel (after the exit button)
      const existing = fsControlsPanel.querySelector('.controls');
      if (existing) existing.remove();
      const src = document.querySelector('#controlsBody .controls');
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
      const src = document.getElementById('coordPanel');
      if (src) {
        const clone = src.cloneNode(true);
        // Give cloned elements new ids to avoid conflicts
        clone.id = 'fs-coordPanel-clone';
        clone.querySelectorAll('[id]').forEach(el => {
          el.id = 'fs-clone-' + el.id;
        });
        // Remove collapsed state
        clone.classList.remove('coord-collapsed');
        clone.style.minWidth = '0';
        clone.style.maxWidth = '100%';
        clone.style.marginTop = '0';
        clone.style.background = 'transparent';
        fsPointsPanel.appendChild(clone);
      }
    };

    // Keep fs points panel in sync when the coord table updates
    const coordBody = document.getElementById('coordinatesBody');
    if (coordBody) {
      new MutationObserver(() => {
        if (isFullscreen) populateFsPoints();
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

    // ── Enter / Exit fullscreen ──
    const toggleFullscreen = () => {
      // ── Save zoom & pan BEFORE switching modes ──
      // We record the image-space point at the viewport centre so we can
      // re-centre on the same spot after the viewport geometry changes.
      const vp = document.getElementById('canvasViewport');
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
      fsBtn.textContent = '⛶';
      fsBtn.title = isFullscreen ? 'Exit fullscreen (Alt+F)' : 'Fullscreen mode (Alt+F)';
      fsBtn.style.background = isFullscreen ? '#007bff' : '';
      fsBtn.style.color = isFullscreen ? '#fff' : '';

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
        // Restore normal viewport max-height (CSS position:fixed removed by class toggle)
        if (vp) {
          vp.style.maxHeight = Math.max(300, window.innerHeight - 220) + 'px';
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

    // Escape key exits fullscreen
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
