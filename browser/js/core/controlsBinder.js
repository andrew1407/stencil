import { setVal, notify, matchHotkey, isTypingTarget, hasTextSelection, unitToCm, wireNameEditor, supportsShareFiles } from '../utils.js';
import HOTKEY_DEFS from '../config/hotkeysConfig.json' with { type: 'json' };
import { hotkeys } from './hotkeys.js';
import { enhanceSelect } from '../ui/customSelect.js';
import { icon } from '../ui/icons.js';
import { applyAccentFavicon, normalizeHex, accentHex } from './accents.js';

// ── ControlsBinder: DOM event wiring for the toolbars, keyboard, and canvas ─────
// Extracted from drawingApp.js: the #wire* family that binds every control group to the app's
// public methods (setColor/setPageSize/undo/…). Pure glue — it holds only the three gesture-loop
// bits internal to the wiring (arrow-pan key set + rAF, smooth-zoom animation state); all editor
// state + behavior lives on the back-referenced app. initEventListeners() calls these in a fixed
// source order (document-level listener dispatch order depends on it) via this.controls.wire*().
export class ControlsBinder {
  // Arrow-key panning: held keys + the rAF handle driving the pan loop.
  #arrowsHeld = new Set();
  #arrowPanRaf = null;
  // Smooth wheel-zoom animation state.
  #smoothZoom = { target: null, focal: null, rafId: null };

  constructor(app) {
    this.app = app;
  }

  wireStyleControls() {
    const app = this.app;
    // The unified Open dialog (openImageModal) owns the Open triggers: #load-image-btn
    // (empty state) is its open button; #open-image-btn (image loaded) and the blank
    // shortcuts open the same dialog. The rest of the Image-actions group are direct.
    document.getElementById('copy-image')?.addEventListener('click', () => app.export.copyImageToClipboard());
    const shareBtn = document.getElementById('share-image');
    if (shareBtn) {
      if (supportsShareFiles()) shareBtn.style.display = '';
      shareBtn.addEventListener('click', () => app.export.shareImage());
    }
    document.getElementById('rotate-left').addEventListener('click', () => app.imageModel.rotateImage(-1));
    document.getElementById('rotate-right').addEventListener('click', () => app.imageModel.rotateImage(1));
    document.getElementById('line-color').addEventListener('change', e => app.settings.setColor(e.target.value));
    // Live drag (input) previews without persisting; the trailing change commits.
    document.getElementById('line-thickness').addEventListener('input', e => app.settings.setThickness(e.target.value, { persist: false }));
    document.getElementById('line-thickness').addEventListener('change', e => app.settings.setThickness(e.target.value));
    document.getElementById('marker-size').addEventListener('input', e => app.settings.setMarkerSize(e.target.value, { persist: false }));
    document.getElementById('marker-size').addEventListener('change', e => app.settings.setMarkerSize(e.target.value));
    document.getElementById('line-style').addEventListener('change', e => app.settings.setLineStyle(e.target.value));
    document.getElementById('image-filter').addEventListener('change', e => app.settings.setImageFilter(e.target.value));
    let filterColorTimer = null;
    document.getElementById('filter-color').addEventListener('input', e => {
      // Reflect the model + mirror immediately; debounce the redraw/persist commit.
      app.filterColor = e.target.value;
      const ctxTint = document.getElementById('ctx-tint-color');
      if (ctxTint) ctxTint.value = e.target.value;
      clearTimeout(filterColorTimer);
      filterColorTimer = setTimeout(() => app.settings.setFilterColor(e.target.value), 80);
    });
  }

  wireSelectionPanelControls() {
    const app = this.app;
    document.getElementById('sel-color').addEventListener('input', e => app.applySelectionChange('color', e.target.value));
    document.getElementById('sel-thickness').addEventListener('change', e => app.applySelectionChange('thickness', parseInt(e.target.value)));
    document.getElementById('sel-marker-size').addEventListener('change', e => app.applySelectionChange('marker-size', parseInt(e.target.value)));
    document.getElementById('sel-style').addEventListener('change', e => app.applySelectionChange('style', e.target.value));
    document.getElementById('sel-fill-enabled').addEventListener('change', () => app.applyFill());
    document.getElementById('sel-fill').addEventListener('input', () => {
      document.getElementById('sel-fill-enabled').checked = true;
      app.applyFill();
    });
    document.getElementById('sel-fill-clear').addEventListener('click', () => {
      document.getElementById('sel-fill-enabled').checked = false;
      app.applyFill();
      notify('Fill cleared (transparent)', 'ok');
    });
    document.getElementById('sel-deselect').addEventListener('click', () => app.deselectLine());
  }

  wirePageAndDisplayControls() {
    const app = this.app;
    document.getElementById('page-size').addEventListener('change', e => app.settings.setPageSize(e.target.value));
    document.getElementById('custom-page-width').addEventListener('change', e => {
      // Inputs are typed in the active unit; the setter stores cm.
      const v = parseFloat(e.target.value);
      app.settings.setCustomPageWidth(Number.isNaN(v) ? 21 : unitToCm(v, app.unit));
    });
    document.getElementById('custom-page-height').addEventListener('change', e => {
      const v = parseFloat(e.target.value);
      app.settings.setCustomPageHeight(Number.isNaN(v) ? 29.7 : unitToCm(v, app.unit));
    });
    const unitSel = document.getElementById('unit-select');
    if (unitSel) unitSel.addEventListener('change', e => app.settings.setUnit(e.target.value));
    // Swap the native popups (whose position macOS controls) for custom dropdowns
    // anchored below the control. The native <select>s stay as the state source, so
    // the change listeners above and every setVal('page-size'|'unit-select') keep working.
    // Page size gets a search bar (33 ISO formats — scrolling alone is too slow).
    enhanceSelect(document.getElementById('page-size'), { search: true });
    enhanceSelect(unitSel);
    document.getElementById('show-points').addEventListener('change', e => app.settings.setShowPoints(e.target.checked));
    document.getElementById('show-lines').addEventListener('change', e => app.settings.setShowLines(e.target.checked));
  }

  wireFormulaControls() {
    const app = this.app;
    const validateAndApplyFormulas = () => {
      const fxVal = document.getElementById('formula-x').value.trim();
      const fyVal = document.getElementById('formula-y').value.trim();
      const okX = app.formula.validate(fxVal, 'x');
      const okY = app.formula.validate(fyVal, 'y');
      app.settings.showFormulaError(!okX || !okY);
      if (okX && okY) {
        app.formulaX = fxVal;
        app.formulaY = fyVal;
        setVal('ctx-formula-x', fxVal);
        setVal('ctx-formula-y', fyVal);
        app.settings.refreshFormulaCoords();
        app.storage.save();
        app.remoteSync.scheduleRemoteSync();   // push the formula change to peers/server
      }
    };
    document.getElementById('allow-formulas').addEventListener('change', e => app.settings.setAllowFormulas(e.target.checked));
    document.getElementById('formula-x').addEventListener('input', validateAndApplyFormulas);
    document.getElementById('formula-y').addEventListener('input', validateAndApplyFormulas);
  }

  wireToolbarButtons() {
    const app = this.app;
    // Topbar project-name field: read-only title, renames inline on demand. Double-click
    // (or hover ✎) to edit; ✓/✗ show ONLY while editing. ✓ enabled for a changed, valid
    // (non-empty, unique) name; ✓/Enter commit, ✗/Escape/click-away revert. Incognito / no
    // project never expose ✎ or ✓/✗.
    const nameInput = document.getElementById('project-name-input');
    const nameEdit = document.getElementById('project-name-edit');
    const nameAccept = document.getElementById('project-name-accept');
    const nameCancel = document.getElementById('project-name-cancel');
    if (nameInput && nameAccept && nameCancel) {
      const currentName = () => (app.activeProjectId != null ? (app.storage.store.getMeta(app.activeProjectId)?.name || '') : '');
      // Only a saved (non-incognito) project can be renamed.
      const canRename = () => app.activeProjectId != null && !app.storage.incognito;
      const endEdit = () => {
        app.nameEditing = false;
        nameInput.readOnly = true;
        nameAccept.style.display = 'none';
        nameCancel.style.display = 'none';
        app.updateProjectTitle(true);   // restore value + ✎ visibility
      };
      const beginEdit = () => {
        if (!canRename() || app.nameEditing) return;
        app.nameEditing = true;
        nameInput.readOnly = false;
        if (nameEdit) nameEdit.style.display = 'none';
        const colorBtn = document.getElementById('project-color-btn');
        if (colorBtn) colorBtn.style.display = 'none';
        nameAccept.style.display = '';
        nameCancel.style.display = '';
        app.nameEditor?.refresh();     // set ✓ enabled/disabled for the starting value
        nameInput.focus();
        nameInput.select();
      };
      app.nameEditor = wireNameEditor(nameInput, nameAccept, nameCancel, {
        alwaysShow: true,                // edit-mode controls ✓/✗ visibility, not change-detection
        current: currentName,
        validate: (v) => app.storage.store.validateName(v, app.activeProjectId),
        commit: (v) => {
          if (app.activeProjectId != null) app.renameProject(app.activeProjectId, v);   // syncs imageBaseName itself
          endEdit();
        },
        cancel: () => endEdit(),
      });
      nameInput.addEventListener('dblclick', () => beginEdit());
      if (nameEdit) nameEdit.addEventListener('click', () => beginEdit());
      // A real click-away (the ✓/✗ buttons prevent their own mousedown, so they don't
      // blur) discards the in-progress rename.
      nameInput.addEventListener('blur', () => { if (app.nameEditing) endEdit(); });
    }
    // Project-colour swatch: a native colour picker that paints the project NAME. Live while
    // dragging; a right-click (or holding Alt at open) clears the colour back to the theme accent.
    const colorBtn = document.getElementById('project-color-btn');
    const colorInput = document.getElementById('project-color-input');
    if (colorBtn && colorInput) {
      const apply = () => {
        if (app.activeProjectId != null) app.setProjectColor(app.activeProjectId, colorInput.value);
      };
      colorInput.addEventListener('input', apply);   // live while dragging
      colorInput.addEventListener('change', apply);  // final commit
      const openPicker = () => {
        const cur = app.storage.store.getMeta(app.activeProjectId)?.color || '';
        colorInput.value = normalizeHex(cur) || accentHex(app.accent);
        try {
          if (typeof colorInput.showPicker === 'function') colorInput.showPicker();
          else colorInput.click();
        } catch {
          colorInput.click();
        }
      };
      // Click opens a small menu so resetting to the neutral default is a visible choice — not a
      // hidden right-click: "Choose colour…" opens the native picker, "Default (no colour)" clears
      // it. (Right-click still clears as a shortcut.)
      colorBtn.addEventListener('click', e => {
        if (app.activeProjectId == null || app.storage.incognito) return;
        e.stopPropagation();
        const open = document.getElementById('project-color-menu');
        if (open) { open.remove(); return; }   // toggle off
        const menu = document.createElement('div');
        menu.id = 'project-color-menu';
        menu.className = 'project-menu';
        const item = (ic, label, onClick) => {
          const b = document.createElement('button');
          b.className = 'project-menu-item btn-icon-text';
          b.innerHTML = `${icon(ic, { size: 15 })}<span>${label}</span>`;
          b.addEventListener('click', ev => { ev.stopPropagation(); menu.remove(); onClick(); });
          menu.appendChild(b);
        };
        item('palette', 'Choose colour…', openPicker);
        if (app.storage.store.getMeta(app.activeProjectId)?.color)
          item('x', 'Default (no colour)', () => app.setProjectColor(app.activeProjectId, ''));
        document.body.appendChild(menu);
        const r = colorBtn.getBoundingClientRect();
        const mw = menu.offsetWidth;
        menu.style.left = `${Math.max(8, Math.min(r.right - mw, window.innerWidth - mw - 8))}px`;
        menu.style.top = `${r.bottom + 6}px`;
        const close = () => { menu.remove(); document.removeEventListener('mousedown', onDoc, true); };
        const onDoc = ev => { if (!menu.contains(ev.target)) close(); };
        setTimeout(() => document.addEventListener('mousedown', onDoc, true), 0);
      });
      // Right-click the swatch clears the custom colour (back to the neutral default).
      colorBtn.addEventListener('contextmenu', e => {
        e.preventDefault();
        if (app.activeProjectId != null) app.setProjectColor(app.activeProjectId, '');
      });
    }
    document.getElementById('start-drawing').addEventListener('click', () => app.startDrawingMode());
    document.getElementById('stop-drawing').addEventListener('click', () => app.stopDrawingMode());
    document.getElementById('draw-mode-toggle').addEventListener('click', () => {
      app.setDrawMode(app.drawMode === 'rect' ? 'line' : 'rect');
      app.storage.save();
    });
    document.getElementById('undo').addEventListener('click', () => app.undo());
    document.getElementById('redo').addEventListener('click', () => app.redo());
    document.getElementById('download-json').addEventListener('click', () => app.export.downloadJSON());
    document.getElementById('copy-json-btn').addEventListener('click', () => app.export.copyLayoutToClipboard());
    document.getElementById('save-image').addEventListener('click', () => app.export.saveImage());
    document.getElementById('upload-json-btn').addEventListener('click', () => document.getElementById('upload-json').click());
    document.getElementById('upload-json').addEventListener('change', e => app.export.uploadJSON(e));
    document.getElementById('clear-storage').addEventListener('click', async () => {
      if (app.storage.temporary || app.activeProjectId == null) {
        // Temporary editor → just clear the editor back to blank.
        if (await app.confirm('Clear this editor (image + lines)?', { title: 'Clear editor', danger: true })) {
          app.storage.newTemporary();
          app.tabs.reportActive(null);
          app.showSaveStatus('Cleared', 'var(--danger)', 'trash');
        }
        return;
      }
      // A server-linked project only clears its LOCAL copy — the server keeps it.
      // Say so up front, and confirm the user really wants to drop the open project.
      const server = app.remoteLink?.address;
      const msg = server
        ? `Remove the local copy of this project? It is stored on the server ${server} and will stay there.`
        : 'Clear this project (image + lines) from storage?';
      if (await app.confirm(msg, { title: server ? 'Remove local copy' : 'Clear project', danger: true })) {
        const id = app.activeProjectId;
        app.storage.store.remove(id);
        app.storage.newTemporary();
        app.remoteLink = null;   // dropped the local session → no server link to save back to
        app.tabs.reportActive(null);
        app.tabs.projectsChanged({ id, action: PROJECT_ACTION.REMOVED });
        if (server) notify(`Local copy removed — still on the server ${server}`, 'info');
        app.showSaveStatus('Cleared', 'var(--danger)', 'trash');
      }
    });
    const incognitoBtn = document.getElementById('incognito-toggle');
    if (incognitoBtn) incognitoBtn.addEventListener('click', () => {
      if (!app.canToggleIncognito()) return;
      app.storage.incognito = !app.storage.incognito;
      app.updateIncognitoUI();
      notify(app.storage.incognito
        ? 'Incognito mode — this editor won\'t be saved'
        : 'Incognito off', 'info');
    });
    document.getElementById('clear-all-lines').addEventListener('click', () => app.clearAllLines());
    // Zoom buttons: single click = small step, double-click = large step,
    // hold = continuous zoom (kicks in after a short delay)
    app.zoomPan.setupHoldZoom(document.getElementById('zoom-in'), +1);
    app.zoomPan.setupHoldZoom(document.getElementById('zoom-out'), -1);
    document.getElementById('zoom-fit').addEventListener('click', () => app.zoomPan.fitToWindow());
  }

  wireZoomControls() {
    const app = this.app;
    // Manual zoom input
    const zoomInput = document.getElementById('zoom-input');
    const applyZoomInput = () => {
      const val = parseFloat(zoomInput.value);
      if (!isNaN(val) && val >= 5 && val <= 500) {
        app.zoomPan.zoomAroundCenter(val / 100);
      } else {
        zoomInput.value = Math.round(app.scale * 100);
      }
    };
    zoomInput.addEventListener('change', applyZoomInput);
    zoomInput.addEventListener('keydown', e => {
      if (e.key === 'Enter') { e.preventDefault(); applyZoomInput(); zoomInput.blur(); }
      if (e.key === 'Escape') { zoomInput.value = Math.round(app.scale * 100); zoomInput.blur(); }
    });
    // Prevent zoom input scroll from zooming the canvas
    zoomInput.addEventListener('wheel', e => e.stopPropagation());
  }

  wireScrollPersist() {
    const app = this.app;
    // Save scroll position (debounced) so it's restored on reopen
    {
      const scrollVp = document.getElementById('canvas-viewport');
      if (scrollVp) {
        let scrollSaveTimer = null;
        scrollVp.addEventListener('scroll', () => {
          clearTimeout(scrollSaveTimer);
          scrollSaveTimer = setTimeout(() => app.storage.save(), 400);
        });
      }
    }
  }

  wireTheme() {
    const app = this.app;
    app.accents.updateThemeIcon();
    // Tint the tab favicon + status bar to the saved accent on load.
    applyAccentFavicon(app.accent);
    document.getElementById('theme-toggle').addEventListener('click', () => {
      app.setTheme(app.theme === 'dark' ? 'light' : 'dark');
    });
    // Follow system changes only if user hasn't manually overridden
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', e => {
      if (!localStorage.getItem('drawingApp_theme')) {
        document.documentElement.setAttribute('data-theme', e.matches ? 'dark' : 'light');
        app.accents.updateThemeIcon();
      }
    });
  }

  wireKeyboard() {
    const app = this.app;
    // Click a control by id only when it exists and isn't disabled (mirrors a real UI click).
    const clickIfActive = id => {
      const el = document.getElementById(id);
      if (el && !el.disabled) el.click();
    };
    // Keyboard shortcuts — dispatched via the hotkeys registry
    const HK_HANDLERS = {
      undo: () => { if (!document.getElementById('undo').disabled) app.undo(); },
      redo: () => { if (!document.getElementById('redo').disabled) app.redo(); },
      startDraw: () => { if (app.image && !app.isDrawing) app.startDrawingMode(); },
      stopDraw: () => { if (app.isDrawing) app.stopDrawingMode(); },
      togglePoints: () => {
        const cb = document.getElementById('show-points');
        cb.checked = !cb.checked;
        app.showPoints = cb.checked;
        app.renderer.redraw();
      },
      toggleLines: () => {
        const cb = document.getElementById('show-lines');
        cb.checked = !cb.checked;
        app.showLines = cb.checked;
        app.renderer.redraw();
      },
      cycleFilter: () => {
        const opts = ['none', 'bw', 'sepia', 'invert', 'contour', 'custom'];
        const cur = opts.indexOf(app.imageFilter);
        // Route through setImageFilter so the cycle marks the filter dirty + syncs to
        // the server (it used to set the value inline and never push).
        app.settings.setImageFilter(opts[(cur + 1) % opts.length]);
      },
      resetZoom: () => app.zoomPan.fitToWindow(),
      toggleControls: () => { const b = document.getElementById('toggle-controls');   if (b) b.click(); },
      togglePointsList: () => { const b = document.getElementById('toggle-coord-panel'); if (b) b.click(); },
      fullscreen: () => app.toggleFullscreen?.(),
      zoomIn: () => app.zoomPan.zoomAroundCenter(app.scale + 0.25),
      zoomOut: () => app.zoomPan.zoomAroundCenter(app.scale - 0.25),
      zoomInBig: () => app.zoomPan.zoomAroundCenter(app.scale + 1.0),
      zoomOutBig: () => app.zoomPan.zoomAroundCenter(app.scale - 1.0),
      rotateImageLeft: () => { if (app.image) app.imageModel.rotateImage(-1); },
      rotateImageRight: () => { if (app.image) app.imageModel.rotateImage(1); },
      copyImage: () => app.export.copyImageToClipboard(),
      copyLayout: () => app.export.copyLayoutToClipboard(),
      // paste is handled by the native 'paste' event listener below — entry here is for hotkey display only
      paste: () => { /* handled by paste event */ },
      clearAllLines: () => app.clearAllLines(),
      // Delete the selected line; on Mac the default reads as ⌥⌫ (Delete→Backspace).
      deleteLine: () => { if (!app.isDrawing && app.selectedLineIdx >= 0) app.removeLine(app.selectedLineIdx); },
      // Delete the focused point of the selected line (the point, not the whole line).
      deletePoint: () => {
        if (app.isDrawing) return;
        if (app.coordLineIdx >= 0 && app.focusedPtIdx >= 0) app.removePoint(app.coordLineIdx, app.focusedPtIdx);
      },
      // Toolbar/menu openers — each just drives the matching button so the shortcut and the
      // click path stay identical (clickIfActive skips a disabled control, like the UI does).
      loadImage: () => clickIfActive(app.image ? 'open-image-btn' : 'load-image-btn'),
      openAnotherImage: () => clickIfActive(app.image ? 'open-image-btn' : 'load-image-btn'),
      openIn: () => clickIfActive('open-in-btn'),
      saveImage: () => clickIfActive('save-image'),
      cropImage: () => clickIfActive('crop-image'),
      downloadJson: () => clickIfActive('download-json'),
      uploadJson: () => clickIfActive('upload-json-btn'),
      openProjects: () => clickIfActive('projects-btn'),
      openServers: () => clickIfActive('connect-btn'),
      openLinks: () => clickIfActive('links-btn'),
      toggleTheme: () => clickIfActive('theme-toggle'),
      toggleIncognito: () => clickIfActive('incognito-toggle'),
      openHelp: () => clickIfActive('info-btn')
    };
    document.addEventListener('keydown', e => {
      if (isTypingTarget(e.target)) return;
      for (const def of HOTKEY_DEFS) {
        const combo = hotkeys.get(def.id);
        if (!combo) continue;
        if (!matchHotkey(e, combo)) continue;
        // Skip 'paste' here — let the browser fire its native paste event
        if (def.id === 'paste') return;
        // With text selected, let the native Ctrl+C / Ctrl+Alt+C copy that text instead
        // of hijacking for copy-image / copy-layout (the user is copying a URL/label).
        if ((def.id === 'copyImage' || def.id === 'copyLayout') && hasTextSelection()) return;
        e.preventDefault();
        const fn = HK_HANDLERS[def.id];
        if (fn) fn();
        return;
      }

      // Alt + (=/+/−) ergonomic zoom shortcuts (not in the registry so they can't be remapped away)
      // Shift held → large step (1.0), otherwise small step (0.25)
      if (e.altKey && !e.ctrlKey && !e.metaKey) {
        const inc = e.code === 'Equal' || e.code === 'NumpadAdd';
        const dec = e.code === 'Minus' || e.code === 'NumpadSubtract';
        if (inc || dec) {
          e.preventDefault();
          const step = e.shiftKey ? 1.0 : 0.25;
          app.zoomPan.zoomAroundCenter(app.scale + (inc ? step : -step));
        }
      }
    });

    // Refresh tooltip & cursor the instant a modifier key is pressed/released
    // while hovering the canvas — so Shift (full points list) and Ctrl (live
    // cursor coordinates) tooltips update at once without re-hovering.
    const onModifierChange = e => {
      if (e.key !== 'Shift' && e.key !== 'Control' && e.key !== 'Alt' && e.key !== 'Meta') return;
      const mods = { ctrlKey: e.ctrlKey, shiftKey: e.shiftKey, altKey: e.altKey, metaKey: e.metaKey };
      app.tooltipMgr.refresh(mods);
      // Live-switch an active segment/line drag the instant Shift is pressed or
      // released, even if the mouse is held still.
      if ((app.isDraggingSegment && app.draggingSegment) ||
          (app.isDraggingLine && app.draggingLine)) {
        app.dragMove(app.lastMouseClientX, app.lastMouseClientY, mods.shiftKey);
      }
      if (app.mouseOverCanvas && !app.isZoomRectDragging && !app.isPanning &&
        !app.isDraggingPoint && !app.isDraggingSegment && !app.isDraggingLine) {
        if (mods.altKey)                         app.canvas.style.cursor = 'grab';
        else if ((mods.ctrlKey || mods.metaKey) && !mods.shiftKey) app.canvas.style.cursor = 'copy';
        else if (mods.shiftKey)                  app.canvas.style.cursor = 'zoom-in';
        else                                     app.canvas.style.cursor = 'crosshair';
      }
    };
    document.addEventListener('keydown', onModifierChange);
    document.addEventListener('keyup', onModifierChange);
  }

  wireArrowPan() {
    const app = this.app;
    // ── Arrow-key panning ──────────────────────────────────────
    // Plain arrows pan the viewport; multiple arrows pan diagonally, opposing pairs
    // cancel; Shift accelerates. Alt/Ctrl/Meta are reserved (e.g. Alt+ArrowUp = zoom).
    this.#arrowsHeld = new Set();
    this.#arrowPanRaf = null;
    const ARROW_KEYS = ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'];
    const arrowPanTick = () => {
      const vp = document.getElementById('canvas-viewport');
      if (!vp || this.#arrowsHeld.size === 0) { this.#arrowPanRaf = null; return; }
      const speed = this.#arrowsHeld.has('Shift') ? 22 : 7;
      let dx = 0;
      let dy = 0;
      if (this.#arrowsHeld.has('ArrowLeft'))  dx -= 1;
      if (this.#arrowsHeld.has('ArrowRight')) dx += 1;
      if (this.#arrowsHeld.has('ArrowUp'))    dy -= 1;
      if (this.#arrowsHeld.has('ArrowDown'))  dy += 1;
      if (dx) vp.scrollLeft += dx * speed;
      if (dy) vp.scrollTop  += dy * speed;
      this.#arrowPanRaf = requestAnimationFrame(arrowPanTick);
    };
    document.addEventListener('keydown', e => {
      if (isTypingTarget(e.target)) return;
      if (e.key === 'Shift') { this.#arrowsHeld.add('Shift'); return; }
      if (!ARROW_KEYS.includes(e.key)) return;
      // Don't steal Alt/Ctrl/Meta+Arrow combos used by other shortcuts
      if (e.ctrlKey || e.altKey || e.metaKey) return;
      e.preventDefault();
      this.#arrowsHeld.add(e.key);
      if (!this.#arrowPanRaf) this.#arrowPanRaf = requestAnimationFrame(arrowPanTick);
    });
    document.addEventListener('keyup', e => {
      if (e.key === 'Shift') this.#arrowsHeld.delete('Shift');
      if (ARROW_KEYS.includes(e.key)) this.#arrowsHeld.delete(e.key);
    });
    // Clear held keys if the window loses focus while arrows are held
    window.addEventListener('blur', () => { this.#arrowsHeld.clear(); });
  }

  wireDropPaste() {
    const app = this.app;
    // Document-wide drag-and-drop overlay
    const dropZone = document.getElementById('global-drop-overlay');

    document.addEventListener('dragenter', e => {
      e.preventDefault();
      if (e.dataTransfer.types.includes('Files')) dropZone.style.display = 'flex';
    });
    document.addEventListener('dragover', e => {
      e.preventDefault();
      e.dataTransfer.dropEffect = 'copy';
    });
    document.addEventListener('dragleave', e => {
      // Only hide when leaving the entire window
      if (e.relatedTarget === null) dropZone.style.display = 'none';
    });
    document.addEventListener('drop', e => {
      e.preventDefault();
      dropZone.style.display = 'none';
      const file = e.dataTransfer.files[0];
      if (!file) return;
      if (file.type.startsWith('image/')) {
        app.loadImageFromFile(file);
      } else if (file.name.endsWith('.json') || file.type === 'application/json') {
        app.loadJSONFromFile(file);
      } else {
        notify('Please drop an image or a .json file', 'fail');
      }
    });

    // Clipboard paste (Ctrl+V) — handles images and JSON layout text
    document.addEventListener('paste', async e => {
      if (isTypingTarget(e.target)) return; // let native paste work in inputs
      const cd = e.clipboardData;
      if (!cd) return;

      // 1) Image takes priority
      for (const item of cd.items) {
        if (item.type && item.type.startsWith('image/')) {
          e.preventDefault();
          // Read the file synchronously — clipboardData is invalid after an await.
          const file = item.getAsFile();
          if (app.image && !(await app.confirm('Replace current image with pasted image?', { title: 'Replace image' }))) {
            notify('Image paste canceled', 'fail');
            return;
          }
          if (file) {
            app.loadImageFromFile(file);
            notify('Image pasted from clipboard', 'ok');
          } else {
            notify('Could not read pasted image', 'fail');
          }
          return;
        }
      }

      // 2) Text — try to parse as layout JSON
      const text = cd.getData('text/plain');
      if (text) {
        let data = null;
        try {
          data = JSON.parse(text);
        } catch {
          /* not layout JSON — left as null; the guard below simply ignores the paste */
        }
        if (data && Array.isArray(data.lines)) {
          e.preventDefault();
          app.export.applyPastedLayout(data);
        }
      }
    });
  }

  wireCanvasPointer() {
    const app = this.app;
    app.canvas.addEventListener('click', e => app.canvasClick(e));
    app.canvas.addEventListener('dblclick', e => app.canvasDblClick(e));
    app.canvas.addEventListener('mousemove', e => app.canvasMouseMove(e));
    app.canvas.addEventListener('mouseleave', () => {
      app.mouseOverCanvas = false;
      app.tooltipMgr.hide();
      app.updateCoordStatus();
      if (app.hoverPt) { app.hoverPt = null; app.renderer.redraw(); }
    });
  }

  wireSmoothZoom() {
    const app = this.app;
    // ── Smooth zoom via rAF ──
    // Rapid wheel events accumulate into one rAF loop. IMPORTANT: add `zoom-no-transition`
    // while the rAF runs so the CSS width/height transition doesn't fight it (causes flicker).
    this.#smoothZoom = { target: null, focal: null, rafId: null };

    const viewport = document.getElementById('canvas-viewport');

    const runSmoothZoom = () => {
      const sz = this.#smoothZoom;
      const oldScale = app.scale;
      const diff = sz.target - oldScale;

      if (Math.abs(diff) < 0.0018) {
        // Snap to final target, re-enable CSS transition, persist
        app.canvas.classList.remove('zoom-no-transition');
        app.zoomPan.setZoom(sz.target, true);
        // Apply final focal-point scroll after snap
        if (sz.focal && viewport) {
          const { imgX, imgY, clientX, clientY } = sz.focal;
          viewport.scrollLeft = imgX * sz.target - clientX;
          viewport.scrollTop = imgY * sz.target - clientY;
        }
        sz.rafId = null;
        sz.focal = null;
        sz.target = null;
        return;
      }

      // Ease towards target (~0.25 per frame — smooth but responsive)
      const next = oldScale + diff * 0.25;
      // Set scale directly (CSS transition is OFF — no conflict)
      app.scale = next;
      app.canvas.style.width = (app.canvas.width  * next) + 'px';
      app.canvas.style.height = (app.canvas.height * next) + 'px';
      app.zoomPan.setZoomInputValue(Math.round(next * 100));

      // Maintain focal point: keep the image pixel under the cursor fixed.
      // scrollLeft = imgX * newScale - clientX_in_viewport
      if (sz.focal && viewport) {
        const { imgX, imgY, clientX, clientY } = sz.focal;
        viewport.scrollLeft = imgX * next - clientX;
        viewport.scrollTop = imgY * next - clientY;
      }

      sz.rafId = requestAnimationFrame(runSmoothZoom);
    };

    // Ctrl+wheel (also fired by touchpad pinch-to-zoom) OR Alt+wheel → zoom toward cursor.
    // The +/− buttons use zoomAroundCenter() instead, so center-zoom is still reachable.
    document.addEventListener('wheel', e => {
      if (!e.ctrlKey && !e.altKey && !e.metaKey) return;
      // No image → nothing to zoom/thicken/rotate; let plain scroll pass through.
      if (!app.image) return;
      // Only act when the wheel is over the canvas viewport — otherwise Ctrl/Alt+wheel over a
      // panel, the projects list, the coord table, etc. would hijack the scroll to zoom the canvas.
      if (!viewport || !viewport.contains(e.target)) return;

      // Alt+wheel → adjust thickness of the line under the cursor
      // (point's line if hovering a point, else the hovered line).
      if (e.altKey && !e.ctrlKey && !e.metaKey) {
        e.preventDefault();
        app.adjustThicknessAtCursor(e);
        return;
      }

      // Ctrl+Shift+wheel with a selected line → rotate it (around its center,
      // or around the focused point if one is selected).
      if ((e.ctrlKey || e.metaKey) && e.shiftKey && app.selectedLineIdx >= 0
          && app.lines[app.selectedLineIdx]) {
        e.preventDefault();
        const dir = e.deltaY > 0 ? 1 : -1;
        app.rotateSelectedLine(dir * (Math.PI / 60)); // 3° per tick
        return;
      }

      e.preventDefault();

      // Per-event zoom increment scaled by the wheel delta (not a fixed step):
      // a mouse "notch" sends a large delta and steps a sensible amount, while a
      // touchpad pinch sends many tiny deltas that now sum gently instead of each
      // leaping a full step (the "too rapid" touchpad zoom). deltaMode normalizes
      // line/page units to pixels; the cap keeps a big mouse notch from overshooting.
      const px = e.deltaY * (e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? 400 : 1);
      const factor = e.shiftKey ? 0.0095 : 0.004;
      const cap = e.shiftKey ? 0.65 : 0.32;
      const delta = Math.max(-cap, Math.min(cap, -px * factor));
      const sz = this.#smoothZoom;

      // Accumulate target scale
      const base = sz.target !== null ? sz.target : app.scale;
      sz.target = Math.max(0.05, Math.min(5, base + delta));

      // Focal point in image-space (unscaled pixels).
      // Works in both fullscreen (viewport fixed at 0,0) and normal mode.
      const vpRect = viewport.getBoundingClientRect();
      const contentX = e.clientX - vpRect.left + viewport.scrollLeft;
      const contentY = e.clientY - vpRect.top  + viewport.scrollTop;
      sz.focal = {
        imgX: contentX / app.scale,
        imgY: contentY / app.scale,
        // cursor position relative to viewport left/top edge (viewport-local)
        clientX: e.clientX - vpRect.left,
        clientY: e.clientY - vpRect.top,
      };

      // Start animation loop; disable CSS transition first to prevent conflict
      if (!sz.rafId) {
        app.canvas.classList.add('zoom-no-transition');
        sz.rafId = requestAnimationFrame(runSmoothZoom);
      }
    }, { passive: false });
  }
}
