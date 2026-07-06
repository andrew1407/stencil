import { StencilElement, hostTag, define } from './base.js';
import { notify, setRadioGroup, formatCombo, supportsShareFiles } from '../utils.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
// ── Component: custom right-click context menu ──────────────────
const SUBMENU_HIDE_DELAY_MS = 180; // grace period before a submenu closes on mouseleave
const LIVE_SYNC_INTERVAL_MS = 120; // poll cadence to reflect external state while the menu is open
const TINT_DEBOUNCE_MS = 80;       // debounce custom-tint recolor+save while dragging the picker

export class StencilContextMenu extends StencilElement {
  static inner() {
    return `
        <!-- Image / Layout submenu -->
        <div class="ctx-item" id="ctx-layout-menu">
            <span class="ctx-icon">${icon('folder')}</span><span class="ctx-label">Image / Layout</span><span class="ctx-arrow">${icon('chevron-right', { size: 12 })}</span>
            <div class="ctx-sub" id="ctx-layout-sub">
                <div class="ctx-sub-label">Image</div>
                <div class="ctx-item ctx-sub-item" id="ctx-copy-img"><span class="ctx-icon">${icon('copy')}</span><span class="ctx-label">Copy Image</span><span class="ctx-hotkey" data-hk="copyImage">Ctrl+C</span></div>
                <div class="ctx-item ctx-sub-item" id="ctx-paste-img"><span class="ctx-icon">${icon('paste')}</span><span class="ctx-label">Paste Image</span><span class="ctx-hotkey" data-hk="paste">Ctrl+V</span></div>
                <div class="ctx-item ctx-sub-item" id="ctx-dl-img"><span class="ctx-icon">${icon('download')}</span><span class="ctx-label">Download Image</span></div>
                <div class="ctx-item ctx-sub-item" id="ctx-share-img" style="display:none;"><span class="ctx-icon">${icon('share')}</span><span class="ctx-label">Share Image</span></div>
                <div class="ctx-sep"></div>
                <div class="ctx-sub-label">Layout (JSON)</div>
                <div class="ctx-item ctx-sub-item" id="ctx-copy-layout"><span class="ctx-icon">${icon('copy')}</span><span class="ctx-label">Copy Layout</span><span class="ctx-hotkey" data-hk="copyLayout">Ctrl+Alt+C</span></div>
                <div class="ctx-item ctx-sub-item" id="ctx-paste-layout"><span class="ctx-icon">${icon('paste')}</span><span class="ctx-label">Paste Layout</span><span class="ctx-hotkey" data-hk="paste">Ctrl+V</span></div>
                <div class="ctx-item ctx-sub-item" id="ctx-dl-layout"><span class="ctx-icon">${icon('file-text')}</span><span class="ctx-label">Download Layout</span></div>
                <div class="ctx-item ctx-sub-item" id="ctx-ul-layout"><span class="ctx-icon">${icon('upload')}</span><span class="ctx-label">Upload Layout</span></div>
            </div>
        </div>
        <!-- Fullscreen toggle -->
        <div class="ctx-item" id="ctx-fullscreen"><span class="ctx-icon">${icon('maximize')}</span><span class="ctx-label" id="ctx-fs-label">Enter Fullscreen</span><span class="ctx-hotkey" data-hk="fullscreen">Alt+F</span></div>
        <!-- Fit zoom to window -->
        <div class="ctx-item" id="ctx-fit-window"><span class="ctx-icon">${icon('fit')}</span><span class="ctx-label">Fit to Window</span><span class="ctx-hotkey" data-hk="resetZoom">Alt+0</span></div>
        <div class="ctx-sep"></div>
        <!-- Drawing -->
        <div class="ctx-item" id="ctx-draw-toggle"><span class="ctx-icon">${icon('play', { size: 14 })}</span><span class="ctx-label" id="ctx-draw-label">Start Drawing</span><span class="ctx-hotkey" id="ctx-draw-hotkey" data-hk="startDraw">Alt+A</span></div>
        <div class="ctx-item" id="ctx-drawmode-toggle"><span class="ctx-icon">${icon('rect')}</span><span class="ctx-label" id="ctx-drawmode-label">Switch to Rectangle Drawing</span></div>
        <div class="ctx-item" id="ctx-draw-rect"><span class="ctx-icon">${icon('rect-filled')}</span><span class="ctx-label">Draw Rectangle (instant)</span></div>
        <div class="ctx-sep"></div>
        <!-- Toggles -->
        <div class="ctx-item" id="ctx-show-points"><span class="ctx-check" id="ctx-chk-points">${icon('check', { size: 14 })}</span><span class="ctx-label">Show Points</span><span class="ctx-hotkey" data-hk="togglePoints">Alt+P</span></div>
        <div class="ctx-item" id="ctx-show-lines"><span class="ctx-check" id="ctx-chk-lines">${icon('check', { size: 14 })}</span><span class="ctx-label">Show Lines</span><span class="ctx-hotkey" data-hk="toggleLines">Alt+L</span></div>
        <div class="ctx-item" id="ctx-clear-lines"><span class="ctx-icon">${icon('trash')}</span><span class="ctx-label">Clear All Lines</span><span class="ctx-hotkey" data-hk="clearAllLines">Alt+W</span></div>
        <div class="ctx-sep"></div>
        <!-- Style submenu -->
        <div class="ctx-item" id="ctx-style-menu">
            <span class="ctx-icon">${icon('palette')}</span><span class="ctx-label">Style</span><span class="ctx-arrow">${icon('chevron-right', { size: 12 })}</span>
            <div class="ctx-sub" id="ctx-style-sub">
                <div class="ctx-row">
                    <label>Marker Size</label>
                    <input type="number" class="ctx-num" id="ctx-marker-size" min="1" max="30">
                </div>
                <div class="ctx-row">
                    <label>Line Thickness</label>
                    <input type="number" class="ctx-num" id="ctx-thickness" min="1" max="20">
                </div>
                <div class="ctx-sub-label">Line Style</div>
                <div class="ctx-radio-group" id="ctx-style-radios">
                    <label class="ctx-radio-item"><input type="radio" name="ctxLineStyle" value="solid"> Solid</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxLineStyle" value="dashed"> Dashed</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxLineStyle" value="dotted"> Dotted</label>
                </div>
            </div>
        </div>
        <!-- Filter submenu -->
        <div class="ctx-item" id="ctx-filter-menu">
            <span class="ctx-icon">${icon('image')}</span><span class="ctx-label">Image Filter</span><span class="ctx-hotkey" data-hk="cycleFilter">Alt+B</span><span class="ctx-arrow">${icon('chevron-right', { size: 12 })}</span>
            <div class="ctx-sub" id="ctx-filter-sub">
                <div class="ctx-sub-label">Filter</div>
                <div class="ctx-radio-group" id="ctx-filter-radios">
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="none"> None</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="bw"> Black &amp; White</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="sepia"> Sepia</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="invert"> Invert</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="contour"> Contour</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="custom"> Custom Tint</label>
                </div>
                <div id="ctx-tint-row">
                    <label>Tint Color</label>
                    <input type="color" class="ctx-color" id="ctx-tint-color">
                </div>
            </div>
        </div>
        <!-- Transformation submenu -->
        <div class="ctx-item" id="ctx-transform-menu">
            <span class="ctx-icon">${icon('function')}</span><span class="ctx-label">Transformation</span><span class="ctx-arrow">${icon('chevron-right', { size: 12 })}</span>
            <div class="ctx-sub" id="ctx-transform-sub">
                <div class="ctx-sub-label">Coordinate Formulas</div>
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-allow-formulas"> Allow Formulas</label>
                <div id="ctx-formula-inputs" style="display:none;padding:5px 14px 8px;">
                    <div style="margin-top:4px;display:flex;flex-direction:column;gap:5px;">
                        <div style="display:flex;align-items:center;gap:6px;">
                            <label style="font-size:12px;color:var(--text-muted);min-width:36px;font-weight:normal;">x(x)=</label>
                            <input type="text" id="ctx-formula-x" placeholder="e.g. x + 9" style="width:140px;font-family:monospace;font-size:12px;padding:3px 6px;border:1px solid var(--border-main);border-radius:4px;background:var(--input-bg);color:var(--input-text);">
                        </div>
                        <div style="display:flex;align-items:center;gap:6px;">
                            <label style="font-size:12px;color:var(--text-muted);min-width:36px;font-weight:normal;">y(y)=</label>
                            <input type="text" id="ctx-formula-y" placeholder="e.g. (y-7)*4" style="width:140px;font-family:monospace;font-size:12px;padding:3px 6px;border:1px solid var(--border-main);border-radius:4px;background:var(--input-bg);color:var(--input-text);">
                        </div>
                        <div id="ctx-formula-error" style="font-size:11px;color:var(--danger);display:none;align-items:center;gap:5px;">${icon('alert', { size: 13 })} Invalid formula</div>
                    </div>
                </div>
            </div>
        </div>
        <!-- Tooltip submenu -->
        <div class="ctx-item" id="ctx-tooltip-menu">
            <span class="ctx-icon">${icon('message')}</span><span class="ctx-label">Tooltip</span><span class="ctx-arrow">${icon('chevron-right', { size: 12 })}</span>
            <div class="ctx-sub" id="ctx-tooltip-sub">
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-tt-enabled" checked> Show Tooltips</label>
                <div class="ctx-sep"></div>
                <div class="ctx-sub-label">Show in Tooltip</div>
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-tt-page" checked> Page (cm)</label>
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-tt-screen" checked> Screen (px)</label>
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-tt-coords" checked> To Edge (cm)</label>
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-context-menu', 'id="ctx-menu"', StencilContextMenu.inner()); }

  wire(app) {
    const menu = document.getElementById('ctx-menu');
    const canvas = document.getElementById('canvas');
    const viewport = document.getElementById('canvas-viewport');

    // ── Submenu management ──────────────────────────────────────
    let subHideTimer = null;
    let activeSub = null;
    let activeSubItem = null;

    const closeAllSubs = () => {
      clearTimeout(subHideTimer);
      document.querySelectorAll('#ctx-menu .ctx-sub.ctx-sub-visible').forEach(s => s.classList.remove('ctx-sub-visible'));
      document.querySelectorAll('#ctx-menu .ctx-item.ctx-open-sub').forEach(i => i.classList.remove('ctx-open-sub'));
      activeSub = null;
      activeSubItem = null;
    };

    const positionSub = (item, sub) => {
      // Render off-screen to measure, then place correctly
      sub.style.left = '-9999px';
      sub.style.top = '-9999px';
      sub.classList.add('ctx-sub-visible');
      item.classList.add('ctx-open-sub');

      const ir = item.getBoundingClientRect();
      const sw = sub.offsetWidth;
      const sh = sub.offsetHeight;
      const vw = window.innerWidth;
      const vh = window.innerHeight;

      // Default: right side of parent item
      let left = ir.right + 2;
      // Flip left if overflows right edge
      if (left + sw > vw - 6) left = ir.left - sw - 2;
      // Ensure we don't go off-left
      if (left < 4) left = 4;

      // Default: align top with parent item top
      let top = ir.top;
      // Shift up if overflows bottom edge
      if (top + sh > vh - 6) top = vh - sh - 6;
      // Don't go above viewport
      if (top < 4) top = 4;

      sub.style.left = left + 'px';
      sub.style.top = top  + 'px';
    };

    // Reposition helper that reuses the stored active item reference
    const repositionActiveSub = () => {
      if (activeSub && activeSubItem && activeSub.classList.contains('ctx-sub-visible'))
        positionSub(activeSubItem, activeSub);
    };

    // Wire hover handlers — only for TOP-LEVEL items so nested ones inside
    // a submenu don't accidentally close their parent submenu on hover.
    menu.querySelectorAll(':scope > .ctx-item').forEach(item => {
      const sub = item.querySelector(':scope > .ctx-sub');
      if (!sub) {
        // Items without submenus close any open submenu on hover
        item.addEventListener('mouseenter', () => {
          clearTimeout(subHideTimer);
          if (activeSub) {
            activeSub.classList.remove('ctx-sub-visible');
            if (activeSubItem) activeSubItem.classList.remove('ctx-open-sub');
            activeSub = null;
            activeSubItem = null;
          }
        });
        return;
      }

      // ResizeObserver: reposition whenever submenu content changes size
      new ResizeObserver(() => {
        if (sub === activeSub && sub.classList.contains('ctx-sub-visible')) positionSub(item, sub);
      }).observe(sub);

      item.addEventListener('mouseenter', () => {
        clearTimeout(subHideTimer);
        // Close other open subs
        document.querySelectorAll('#ctx-menu .ctx-sub.ctx-sub-visible').forEach(s => {
          if (s !== sub) s.classList.remove('ctx-sub-visible');
        });
        document.querySelectorAll('#ctx-menu .ctx-item.ctx-open-sub').forEach(i => {
          if (i !== item) i.classList.remove('ctx-open-sub');
        });
        positionSub(item, sub);
        activeSub = sub;
        activeSubItem = item;
      });

      // Start hide timer when leaving item but not into submenu
      item.addEventListener('mouseleave', e => {
        if (sub.contains(e.relatedTarget)) return;
        subHideTimer = setTimeout(() => {
          if (activeSub === sub) {
            sub.classList.remove('ctx-sub-visible');
            item.classList.remove('ctx-open-sub');
            activeSub = null; activeSubItem = null;
          }
        }, SUBMENU_HIDE_DELAY_MS);
      });

      // Cancel hide timer when cursor moves into submenu
      sub.addEventListener('mouseenter', () => clearTimeout(subHideTimer));
      sub.addEventListener('mouseleave', e => {
        if (item.contains(e.relatedTarget)) return;
        subHideTimer = setTimeout(() => {
          sub.classList.remove('ctx-sub-visible');
          item.classList.remove('ctx-open-sub');
          if (activeSub === sub) { activeSub = null; activeSubItem = null; }
        }, SUBMENU_HIDE_DELAY_MS);
      });
    });

    // ── Live-sync timer ─────────────────────────────────────────
    // Keeps ctx menu state current while it's open (hotkeys may change state)
    let syncInterval = null;

    const closeMenu = () => {
      menu.classList.remove('ctx-open');
      closeAllSubs();
      clearInterval(syncInterval);
      syncInterval = null;
    };

    const syncState = () => {
      if (!app) return;
      // Draw toggle label
      document.getElementById('ctx-draw-label').textContent =
        app.isDrawing ? 'Stop Drawing' : 'Start Drawing';
      document.getElementById('ctx-draw-toggle').querySelector('.ctx-icon').innerHTML =
        app.isDrawing ? icon('stop', { size: 14 }) : icon('play', { size: 14 });

      // Drawing-mode switch label
      const dmLabel = document.getElementById('ctx-drawmode-label');
      if (dmLabel) dmLabel.textContent = app.drawMode === 'rect'
        ? 'Switch to Line Drawing' : 'Switch to Rectangle Drawing';
      const dmIcon = document.getElementById('ctx-drawmode-toggle').querySelector('.ctx-icon');
      if (dmIcon) dmIcon.innerHTML = app.drawMode === 'rect'
        ? icon('swap') : icon('rect');

      // Toggle a row's disabled state and explain it via the native title tooltip:
      // the base label when enabled, base + "— reason" when off.
      const gate = (id, off, base, reason) => {
        const el = document.getElementById(id);
        if (!el) return;
        el.classList.toggle('ctx-disabled', off);
        el.title = off ? `${base}\n— ${reason}` : base;
      };

      // Download / copy layout enabled only when lines exist
      const hasLines = app.lines && app.lines.length > 0;
      gate('ctx-dl-layout', !hasLines, 'Download Layout', 'Draw at least one line to export');
      gate('ctx-copy-layout', !hasLines, 'Copy Layout', 'Draw at least one line to copy');

      // Copy/download image enabled only when image loaded
      const hasImage = !!app.image;
      gate('ctx-copy-img', !hasImage, 'Copy Image', 'Load an image first');
      gate('ctx-dl-img', !hasImage, 'Download Image', 'Load an image first');
      // Share item is rendered only where Web Share files are supported (see wire()).
      gate('ctx-share-img', !hasImage, 'Share Image', 'Load an image first');
      // Paste-layout needs an image too (matching paste handler behavior)
      gate('ctx-paste-layout', !hasImage, 'Paste Layout', 'Load an image first');

      // Refresh hotkey hint text in case shortcuts were remapped
      hotkeys.updateCtxHints();
      // The draw hotkey span carries data-hk="startDraw"; override it after
      // updateCtxHints so it reflects the start/stop binding for the live state.
      document.getElementById('ctx-draw-hotkey').textContent =
        formatCombo(hotkeys.get(app.isDrawing ? 'stopDraw' : 'startDraw'), hotkeys.isMac);

      // Checkmarks
      document.getElementById('ctx-chk-points').innerHTML = app.showPoints ? icon('check', { size: 14 }) : '';
      document.getElementById('ctx-chk-lines').innerHTML = app.showLines  ? icon('check', { size: 14 }) : '';

      // Style sub values
      document.getElementById('ctx-marker-size').value = app.markerSize;
      document.getElementById('ctx-thickness').value = app.thickness;
      setRadioGroup('ctxLineStyle', app.style);

      // Filter sub values
      setRadioGroup('ctxFilter', app.imageFilter);
      document.getElementById('ctx-tint-color').value = app.filterColor || '#7c3aed';
      const tintRow = document.getElementById('ctx-tint-row');
      tintRow.classList.toggle('ctx-tint-visible', app.imageFilter === 'custom');

      // Fullscreen label
      const isFS = document.body.classList.contains('fullscreen-mode');
      document.getElementById('ctx-fs-label').textContent = isFS ? 'Exit Fullscreen' : 'Enter Fullscreen';
      document.getElementById('ctx-fullscreen').querySelector('.ctx-icon').innerHTML = icon('maximize');

      // Tooltip checkboxes
      document.getElementById('ctx-tt-enabled').checked = app.tooltipEnabled;
      document.getElementById('ctx-tt-page').checked = app.tooltipShowPage;
      document.getElementById('ctx-tt-screen').checked = app.tooltipShowScreen;
      document.getElementById('ctx-tt-coords').checked = app.tooltipShowCoords;
      // Formula checkbox
      document.getElementById('ctx-allow-formulas').checked = app.allowFormulas;
      document.getElementById('ctx-formula-inputs').style.display = app.allowFormulas ? 'block' : 'none';
    };

    const openAt = (x, y) => {
      closeAllSubs();
      menu.style.left = '-9999px'; menu.style.top = '-9999px';
      menu.classList.add('ctx-open');
      const mw = menu.offsetWidth;
      const mh = menu.offsetHeight;
      const vw = window.innerWidth;
      const vh = window.innerHeight;
      x = Math.min(x, vw - mw - 6);
      y = Math.min(y, vh - mh - 6);
      menu.style.left = Math.max(4, x) + 'px';
      menu.style.top = Math.max(4, y) + 'px';
      // Start live-sync so hotkey changes reflect immediately
      clearInterval(syncInterval);
      syncInterval = setInterval(() => {
        if (menu.classList.contains('ctx-open')) syncState();
        else { clearInterval(syncInterval); syncInterval = null; }
      }, LIVE_SYNC_INTERVAL_MS);
    };

    // Right-click on canvas or its viewport wrapper
    [canvas, viewport].forEach(el => {
      el.addEventListener('contextmenu', e => {
        if (!app.image) return;
        e.preventDefault();
        syncState();
        openAt(e.clientX, e.clientY);
      });
    });

    // Close on outside click / Escape / scroll
    document.addEventListener('mousedown', e => {
      if (!menu.contains(e.target)) closeMenu();
    });
    document.addEventListener('keydown', e => {
      if (e.code === 'Escape') closeMenu();
    });
    document.addEventListener('scroll', closeMenu, true);

    // ── Actions ──

    // Copy image to clipboard
    document.getElementById('ctx-copy-img').addEventListener('click', () => {
      closeMenu(); app.export.copyImageToClipboard();
    });

    // Download image
    document.getElementById('ctx-dl-img').addEventListener('click', () => {
      closeMenu(); app.export.saveImage();
    });

    // Share image — only revealed where the Web Share API can share files.
    const shareItem = document.getElementById('ctx-share-img');
    if (shareItem && supportsShareFiles()) shareItem.style.display = '';
    shareItem?.addEventListener('click', () => {
      closeMenu(); app.export.shareImage();
    });

    // Download layout
    document.getElementById('ctx-dl-layout').addEventListener('click', () => {
      closeMenu(); app.export.downloadJSON();
    });

    // Upload layout
    document.getElementById('ctx-ul-layout').addEventListener('click', () => {
      closeMenu(); document.getElementById('upload-json').click();
    });

    // Copy layout JSON to clipboard
    document.getElementById('ctx-copy-layout').addEventListener('click', () => {
      closeMenu(); app.export.copyLayoutToClipboard();
    });

    // Paste image from clipboard (via async Clipboard API)
    document.getElementById('ctx-paste-img').addEventListener('click', async () => {
      closeMenu();
      try {
        const items = await navigator.clipboard.read();
        for (const item of items) {
          const imgType = item.types.find(t => t.startsWith('image/'));
          if (imgType) {
            if (app.image && !(await app.confirm('Replace current image with pasted image?', { title: 'Replace image' }))) {
              notify('Image paste canceled', 'fail');
              return;
            }
            const blob = await item.getType(imgType);
            const file = new File([blob], 'pasted-image.png', { type: imgType });
            app.loadImageFromFile(file);
            notify('Image pasted from clipboard', 'ok');
            return;
          }
        }
        notify('No image found in clipboard', 'fail');
      } catch (err) {
        notify('Clipboard read failed: ' + (err.message || err), 'fail');
      }
    });

    // Paste layout JSON from clipboard
    document.getElementById('ctx-paste-layout').addEventListener('click', async () => {
      closeMenu();
      try {
        const text = await navigator.clipboard.readText();
        if (!text) { notify('Clipboard is empty', 'fail'); return; }
        let data = null;
        try {
          data = JSON.parse(text);
        } catch {
          /* not JSON — left as null, the guard below notifies the user */
        }
        if (!data || !Array.isArray(data.lines)) {
          notify('Clipboard does not contain a layout JSON', 'fail');
          return;
        }
        app.export.applyPastedLayout(data);
      } catch (err) {
        notify('Clipboard read failed: ' + (err.message || err), 'fail');
      }
    });

    // Start / Stop drawing
    document.getElementById('ctx-draw-toggle').addEventListener('click', () => {
      closeMenu();
      if (app.isDrawing) app.stopDrawingMode();
      else if (app.image) app.startDrawingMode();
    });

    // Switch line / rect drawing mode
    document.getElementById('ctx-drawmode-toggle').addEventListener('click', () => {
      closeMenu();
      app.setDrawMode(app.drawMode === 'rect' ? 'line' : 'rect');
      app.storage.save();
      notify('Drawing mode: ' + (app.drawMode === 'rect' ? 'Rectangle' : 'Line'), 'info');
    });

    // Instant draw rectangle: switch to rect mode and start drawing right away.
    // If a line is selected, the next rect connects to it (one-shot).
    document.getElementById('ctx-draw-rect').addEventListener('click', () => {
      closeMenu();
      if (!app.image) { notify('Load an image first', 'fail'); return; }
      app.setDrawMode('rect');
      app.startDrawingMode(); // continues the selected line if one is selected
      notify('Drag to draw a rectangle', 'info');
    });

    // Show points
    document.getElementById('ctx-show-points').addEventListener('click', () => {
      app.showPoints = !app.showPoints;
      const cb = document.getElementById('show-points');
      if (cb) cb.checked = app.showPoints;
      app.renderer.redraw(); app.storage.save();
      document.getElementById('ctx-chk-points').innerHTML = app.showPoints ? icon('check', { size: 14 }) : '';
    });

    // Show lines
    document.getElementById('ctx-show-lines').addEventListener('click', () => {
      app.showLines = !app.showLines;
      const cb = document.getElementById('show-lines');
      if (cb) cb.checked = app.showLines;
      app.renderer.redraw(); app.storage.save();
      document.getElementById('ctx-chk-lines').innerHTML = app.showLines ? icon('check', { size: 14 }) : '';
    });

    // Clear all lines
    document.getElementById('ctx-clear-lines').addEventListener('click', () => {
      closeMenu();
      app.clearAllLines();
    });

    // Marker size — live on input, commit on change
    document.getElementById('ctx-marker-size').addEventListener('input', e => {
      const v = parseInt(e.target.value);
      if (!isNaN(v) && v >= 1 && v <= 30) {
        app.markerSize = v;
        const inp = document.getElementById('marker-size');
        if (inp) inp.value = v;
        app.renderer.redraw();
      }
    });
    document.getElementById('ctx-marker-size').addEventListener('change', e => {
      const v = Math.max(1, Math.min(30, parseInt(e.target.value) || app.markerSize));
      e.target.value = v; app.markerSize = v;
      const inp = document.getElementById('marker-size');
      if (inp) inp.value = v;
      app.renderer.redraw(); app.storage.save();
    });

    // Line thickness — live on input, commit on change
    document.getElementById('ctx-thickness').addEventListener('input', e => {
      const v = parseInt(e.target.value);
      if (!isNaN(v) && v >= 1 && v <= 20) {
        app.thickness = v;
        const inp = document.getElementById('line-thickness');
        if (inp) inp.value = v;
        app.renderer.redraw();
      }
    });
    document.getElementById('ctx-thickness').addEventListener('change', e => {
      const v = Math.max(1, Math.min(20, parseInt(e.target.value) || app.thickness));
      e.target.value = v; app.thickness = v;
      const inp = document.getElementById('line-thickness');
      if (inp) inp.value = v;
      app.renderer.redraw(); app.storage.save();
    });

    // Line style radios
    document.querySelectorAll('input[name="ctxLineStyle"]').forEach(r => {
      r.addEventListener('change', () => {
        app.style = r.value;
        const sel = document.getElementById('line-style');
        if (sel) sel.value = r.value;
        app.renderer.redraw(); app.storage.save();
      });
    });

    // Filter radios
    document.querySelectorAll('input[name="ctxFilter"]').forEach(r => {
      r.addEventListener('change', () => {
        app.imageFilter = r.value;
        const sel = document.getElementById('image-filter');
        if (sel) sel.value = r.value;
        const mainPicker = document.getElementById('filter-color');
        if (mainPicker) mainPicker.style.display = (r.value === 'custom') ? 'inline-block' : 'none';
        document.getElementById('ctx-tint-row').classList.toggle('ctx-tint-visible', r.value === 'custom');
        app.renderer.redraw(); app.storage.save();
      });
    });

    // Tint color picker inside context menu (debounced)
    let ctxTintTimer = null;
    document.getElementById('ctx-tint-color').addEventListener('input', e => {
      app.filterColor = e.target.value;
      const mainPicker = document.getElementById('filter-color');
      if (mainPicker) mainPicker.value = e.target.value;
      clearTimeout(ctxTintTimer);
      ctxTintTimer = setTimeout(() => {
        if (app.imageFilter === 'custom') { app.renderer.redraw(); app.storage.save(); }
      }, TINT_DEBOUNCE_MS);
    });

    // Fullscreen toggle
    document.getElementById('ctx-fullscreen').addEventListener('click', () => {
      closeMenu();
      if (typeof app.toggleFullscreen === 'function') app.toggleFullscreen();
    });

    // Fit to window
    document.getElementById('ctx-fit-window').addEventListener('click', () => {
      closeMenu();
      if (app && app.zoomPan && typeof app.zoomPan.fitToWindow === 'function') app.zoomPan.fitToWindow();
    });

    // Tooltip visibility checkboxes
    document.getElementById('ctx-tt-enabled').addEventListener('change', e => {
      app.tooltipEnabled = e.target.checked;
      if (!app.tooltipEnabled) app.tooltipMgr.hide();
      app.storage.save();
    });
    document.getElementById('ctx-tt-page').addEventListener('change', e => {
      app.tooltipShowPage = e.target.checked;
      app.storage.save();
    });
    document.getElementById('ctx-tt-screen').addEventListener('change', e => {
      app.tooltipShowScreen = e.target.checked;
      app.storage.save();
    });
    document.getElementById('ctx-tt-coords').addEventListener('change', e => {
      app.tooltipShowCoords = e.target.checked;
      app.storage.save();
    });

    // Transformation submenu: formulas
    const ctxSyncFormulaUI = checked => {
      const fi = document.getElementById('formula-inputs');
      const ctxFi = document.getElementById('ctx-formula-inputs');
      const mainCb = document.getElementById('allow-formulas');
      if (fi) fi.style.display = checked ? 'inline-flex' : 'none';
      if (ctxFi) ctxFi.style.display = checked ? 'block' : 'none';
      if (mainCb) mainCb.checked = checked;
    };
    const ctxShowFormulaError = hasError => {
      const el = document.getElementById('formula-error');
      const ctxEl = document.getElementById('ctx-formula-error');
      if (el) el.style.display = hasError ? 'inline' : 'none';
      if (ctxEl) ctxEl.style.display = hasError ? 'block' : 'none';
    };
    const ctxRefreshCoords = () => {
      const li = app.coordLineIdx;
      const pts = li === -1 ? (app.currentLine ? app.currentLine.points : null) : (app.lines[li] ? app.lines[li].points : null);
      app.coordTable.update(pts, li);
    };
    const ctxValidateAndApply = () => {
      const fxVal = (document.getElementById('ctx-formula-x').value || '').trim();
      const fyVal = (document.getElementById('ctx-formula-y').value || '').trim();
      const okX = app.formula.validate(fxVal, 'x');
      const okY = app.formula.validate(fyVal, 'y');
      ctxShowFormulaError(!okX || !okY);
      if (okX && okY) {
        app.formulaX = fxVal; app.formulaY = fyVal;
        const mx = document.getElementById('formula-x');
        const my = document.getElementById('formula-y');
        if (mx) mx.value = fxVal;
        if (my) my.value = fyVal;
        ctxRefreshCoords();
        app.storage.save();
      }
    };
    document.getElementById('ctx-allow-formulas').addEventListener('change', e => {
      app.allowFormulas = e.target.checked;
      ctxSyncFormulaUI(e.target.checked);
      if (!e.target.checked) { app.formulaX = ''; app.formulaY = ''; ctxShowFormulaError(false); }
      ctxRefreshCoords();
      app.storage.save();
    });
    document.getElementById('ctx-formula-x').addEventListener('input', ctxValidateAndApply);
    document.getElementById('ctx-formula-y').addEventListener('input', ctxValidateAndApply);
    menu.addEventListener('mousedown', e => e.stopPropagation());

    // Format the data-hk shortcut spans (hardcoded "Ctrl+C"/"Alt+J" in the markup) right away
    // so macOS shows ⌘/⌥ from the very first paint — not only after the menu is first opened
    // (which is when syncState's poll would otherwise be the first to call updateCtxHints).
    hotkeys.updateCtxHints();
  }
}
define('stencil-context-menu', StencilContextMenu);
