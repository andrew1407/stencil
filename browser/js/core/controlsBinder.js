import { notify, matchHotkey, isTypingTarget, hasTextSelection, unitToCm, wireNameEditor, supportsShareFiles, pointInRect } from '../utils.js';
import HOTKEY_DEFS from '../config/hotkeysConfig.json' with { type: 'json' };
import { hotkeys } from './hotkeys.js';
import { enhanceSelect } from '../ui/customSelect.js';
import { COMPARE_MODES } from './settingsController.js';
import { icon } from '../ui/icons.js';
import { applyAccentFavicon, normalizeHex } from './accents.js';
import { extractDraggedImageUrl, mediaFilesFromData, fetchDraggedMediaFile } from './dragImageUrl.js';
import { showDropOverlay, hideDropOverlay } from '../ui/dropOverlay.js';
import { canvasOrigin } from './zoomPan.js';

// Chords that must work even while a text box has focus. These windows autofocus an
// input, so a blanket typing guard would make their toggles one-way — able to open the
// panel but never close it from inside the box, exactly when the shortcut is wanted.
export const HOTKEYS_WHILE_TYPING = [
  'toggleChat', 'openHelp', 'openHotkeys', 'openVisuals', 'openProjects', 'openServers', 'openLinks',
];

// Which of those a keydown matches while typing, or null for "let the text box have it".
export const typingHotkeyId = (e, hotkeys, ids = HOTKEYS_WHILE_TYPING) => {
  for (const id of ids) {
    const combo = hotkeys.get(id);
    if (combo && matchHotkey(e, combo)) return id;
  }
  return null;
};

// The native colour picker anchors to the INPUT's box, and the 1px hidden inputs sit at
// their positioned ancestor's origin — far from the swatch the user clicked. Pin the
// input just under the invoking button first.
export const anchorPickerInput = (input, btn) => {
  const r = btn?.getBoundingClientRect?.();
  if (!r) return;
  input.style.position = 'fixed';
  input.style.left = `${Math.round(r.left)}px`;
  input.style.top = `${Math.round(r.bottom)}px`;
};



// ── ControlsBinder: DOM event wiring for the toolbars, keyboard, and canvas ─────
// Pure glue: binds each control group to the app's public methods; only the gesture-loop
// bits (arrow-pan keys + rAF, smooth-zoom state) live here. initEventListeners() calls
// these in fixed source order — document-level listener dispatch order depends on it.
export class ControlsBinder {
  // Arrow-key panning: held keys + the rAF handle driving the pan loop.
  #arrowsHeld = new Set();
  #arrowPanRaf = null;
  // Whether R is currently held — the Alt+R+←/→ line-rotate chord.
  #rHeld = false;
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
    document.getElementById('point-color').addEventListener('change', e => app.settings.setPointColor(e.target.value));
    // Live drag (input) previews without persisting; the trailing change commits.
    document.getElementById('line-thickness').addEventListener('input', e => app.settings.setThickness(e.target.value, { persist: false }));
    document.getElementById('line-thickness').addEventListener('change', e => app.settings.setThickness(e.target.value));
    document.getElementById('point-size').addEventListener('input', e => app.settings.setPointSize(e.target.value, { persist: false }));
    document.getElementById('point-size').addEventListener('change', e => app.settings.setPointSize(e.target.value));
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
    document.getElementById('sel-point-color').addEventListener('input', e => app.applySelectionChange('pointColor', e.target.value));
    document.getElementById('sel-thickness').addEventListener('change', e => app.applySelectionChange('thickness', parseInt(e.target.value)));
    document.getElementById('sel-point-size').addEventListener('change', e => app.applySelectionChange('point-size', parseInt(e.target.value)));
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
    // Swap the native popups (whose position and palette macOS controls) for the app's own
    // dropdown; the native <select>s stay the state source, so the change listeners and
    // setVal(...) keep working. Page size gets a search bar (33 ISO formats).
    enhanceSelect(document.getElementById('page-size'), { search: true });
    enhanceSelect(unitSel);
    document.getElementById('show-points').addEventListener('change', e => app.settings.setShowPoints(e.target.checked));
    document.getElementById('show-lines').addEventListener('change', e => app.settings.setShowLines(e.target.checked));
    const compareSel = document.getElementById('compare-mode');
    if (compareSel) compareSel.addEventListener('change', e => app.settings.setCompareMode(e.target.value));
  }

  wireFormulaControls() {
    const app = this.app;
    document.getElementById('allow-formulas').addEventListener('change', e => {
      e.target.closest('.pill-toggle')?.classList.toggle('on', e.target.checked);
      app.settings.setAllowFormulas(e.target.checked);
    });
    // Typing settles → the formula applies (mirrored into the context-menu twins). The
    // debounce and the "don't judge a half-written expression" rule live in the shared
    // wiring; the context menu's pair goes through the same call — see settingsController.
    app.settings.wireFormulaInputs({
      x: 'formula-x', y: 'formula-y', mirrorX: 'ctx-formula-x', mirrorY: 'ctx-formula-y',
    });
  }

  wireToolbarButtons() {
    const app = this.app;
    // Topbar project-name field: double-click (or hover ✎) edits inline; ✓/✗ show ONLY
    // while editing — ✓ enabled for a changed, valid (non-empty, unique) name; ✓/Enter
    // commit, ✗/Escape/click-away revert. Incognito / no project never expose ✎ or ✓/✗.
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
      colorInput.addEventListener('input', apply);
      colorInput.addEventListener('change', apply);
      const openPicker = () => {
        const cur = app.storage.store.getMeta(app.activeProjectId)?.color || '';
        // No custom colour → open at the neutral grey the name is actually painted in (the unset
        // default), not the theme accent, so the picker reflects the real current state.
        colorInput.value = normalizeHex(cur) || '#80868f';
        anchorPickerInput(colorInput, colorBtn);
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
        item('palette', 'Choose color…', openPicker);
        if (app.storage.store.getMeta(app.activeProjectId)?.color)
          item('x', 'Default (no color)', () => app.setProjectColor(app.activeProjectId, ''));
        document.body.appendChild(menu);
        const r = colorBtn.getBoundingClientRect();
        const mw = menu.offsetWidth;
        menu.style.left = `${Math.max(8, Math.min(r.right - mw, window.innerWidth - mw - 8))}px`;
        menu.style.top = `${r.bottom + 6}px`;
        const close = () => { menu.remove(); document.removeEventListener('mousedown', onDoc, true); };
        const onDoc = ev => { if (!menu.contains(ev.target)) close(); };
        setTimeout(() => document.addEventListener('mousedown', onDoc, true), 0);
      });
      colorBtn.addEventListener('contextmenu', e => {
        e.preventDefault();
        if (app.activeProjectId != null) app.setProjectColor(app.activeProjectId, '');
      });
    }

    // Blank-background colour swatch: recolours the active BLANK project's solid fill in place
    // (the drawn lines stay). Shown only for blank projects (gated in updateProjectTitle). A plain
    // native picker — click opens it, live input recolours; no "clear" (a blank always has a fill).
    const blankBtn = document.getElementById('blank-color-btn');
    const blankInput = document.getElementById('blank-color-input');
    if (blankBtn && blankInput) {
      const applyBlank = () => { if (app.activeIsBlank()) app.setBlankColor(blankInput.value); };
      blankInput.addEventListener('input', applyBlank);
      blankInput.addEventListener('change', applyBlank);
      blankBtn.addEventListener('click', e => {
        if (!app.activeIsBlank()) return;
        e.stopPropagation();
        blankInput.value = normalizeHex(app.blankColor) || '#ffffff';
        anchorPickerInput(blankInput, blankBtn);
        try {
          if (typeof blankInput.showPicker === 'function') blankInput.showPicker();
          else blankInput.click();
        } catch {
          blankInput.click();
        }
      });
    }

    // One button, both directions — the same branch the context menu's draw toggle uses.
    document.getElementById('draw-toggle').addEventListener('click', () => {
      if (app.isDrawing) app.stopDrawingMode();
      else app.startDrawingMode();
    });
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
    // Shift+click saves without embedding the light/dark + accent theme (a portable, theme-neutral
    // .stencil); a plain click carries it. The hotkey/synthetic-click path has no shiftKey → theme in.
    document.getElementById('save-project-btn').addEventListener('click', e => app.export.saveProjectFile({ includeTheme: !e.shiftKey }));
    document.getElementById('open-project-btn').addEventListener('click', () => app.export.pickAndOpenProjectFile());
    document.getElementById('live-sync-btn').addEventListener('click', () => { app.stencilSync.liveSync = !app.stencilSync.liveSync; });
    document.getElementById('delete-project-btn').addEventListener('click', () => app.export.deleteProjectFile());
    document.getElementById('clear-storage').addEventListener('click', async () => {
      if (app.storage.temporary || app.activeProjectId == null) {
        // Temporary editor → just clear the editor back to blank.
        if (await app.confirm('Clear this editor (image + lines)?', { title: 'Clear editor', danger: true, confirmIcon: 'trash' })) {
          app.storage.newTemporary();
          app.tabs.reportActive(null);
          // The clear worked, so it reads as a success — a red ✕ said the opposite.
          // Wording matches the desktop's two branches (mainWindow.cpp clearProject).
          app.showSaveStatus('Editor cleared', 'var(--success)', 'check');
        }
        return;
      }
      // A server-linked project only clears its LOCAL copy — the server keeps it.
      // Say so up front, and confirm the user really wants to drop the open project.
      const server = app.remoteLink?.address;
      const msg = server
        ? `Remove the local copy of this project? It is stored on the server ${server} and will stay there.`
        : 'Clear this project (image + lines) from storage?';
      if (await app.confirm(msg, { title: server ? 'Remove local copy' : 'Clear project', danger: true, confirmIcon: 'trash' })) {
        app.remoteLink = null;   // dropped the local session → no server link to save back to
        // ONE removal path (drawingApp.removeProject): storage, the stored chat (§12.2),
        // the drop to a blank editor and the cross-tab notify all happen there.
        app.removeProject(app.activeProjectId);
        if (server) notify(`Local copy removed — still on the server ${server}`, 'info');
        app.showSaveStatus('Project cleared', 'var(--success)', 'check');
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
    const zoomInput = document.getElementById('zoom-input');
    // Apply the typed zoom. `commit` (change/Enter/preset-pick) reverts an out-of-range value to
    // the current zoom; live typing (input) just skips invalid/partial values so it never fights
    // the caret. Accepts the full 5%–3200% range (matches the input max, presets, core clampScale).
    const applyZoomInput = (commit = false) => {
      const val = parseFloat(zoomInput.value);
      if (!isNaN(val) && val >= 5 && val <= 3200) {
        app.zoomPan.zoomAroundCenter(val / 100);
      } else if (commit) {
        zoomInput.value = Math.round(app.scale * 100);
      }
    };
    // Live update as you type (matches the desktop combobox, which applies on every keystroke),
    // plus a committing pass on change/Enter that also snaps back an invalid final value.
    zoomInput.addEventListener('input', () => applyZoomInput(false));
    zoomInput.addEventListener('change', () => applyZoomInput(true));

    // ── Preset dropdown: opens on focus/click, so the input offers BOTH free typing and
    // one-click preset selection (the native datalist on number inputs is unreliable). ──
    const ZOOM_PRESETS = [10, 25, 50, 75, 100, 125, 150, 200, 300, 400, 500, 800, 1600, 3200];
    const zoomMenu = document.getElementById('zoom-menu');
    const openZoomMenu = () => {
      if (!zoomMenu || zoomInput.disabled) return;
      const cur = Math.round(app.scale * 100);
      zoomMenu.innerHTML = ZOOM_PRESETS.map(p =>
        `<div class="zoom-menu-item${p === cur ? ' active' : ''}" data-val="${p}" role="option">${p}%</div>`).join('');
      zoomMenu.hidden = false;
    };
    const closeZoomMenu = () => { if (zoomMenu) zoomMenu.hidden = true; };
    zoomInput.addEventListener('focus', openZoomMenu);
    zoomInput.addEventListener('click', openZoomMenu);
    if (zoomMenu) {
      // mousedown (not click) so it fires before the input's blur hides the menu.
      zoomMenu.addEventListener('mousedown', e => {
        const item = e.target.closest('.zoom-menu-item');
        if (!item) return;
        e.preventDefault();
        zoomInput.value = item.dataset.val;
        applyZoomInput(true);
        closeZoomMenu();
        zoomInput.blur();
      });
    }
    zoomInput.addEventListener('blur', () => setTimeout(closeZoomMenu, 120));
    document.addEventListener('click', e => { if (!e.target.closest('.zoom-input-wrap')) closeZoomMenu(); });

    zoomInput.addEventListener('keydown', e => {
      if (e.key === 'Enter') { e.preventDefault(); applyZoomInput(true); closeZoomMenu(); zoomInput.blur(); }
      if (e.key === 'Escape') { zoomInput.value = Math.round(app.scale * 100); closeZoomMenu(); zoomInput.blur(); }
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
    document.getElementById('theme-toggle').addEventListener('click', (e) => {
      // Hand the wipe the button that was actually pressed — the fullscreen layer clones
      // this toolbar with duplicate ids, so looking it up by id can find a hidden copy.
      app.setTheme(app.theme === 'dark' ? 'light' : 'dark', e.currentTarget);
    });
    // Follow the OS while the appearance mode is 'system' — the default, and what the app
    // stays on until someone picks Light or Dark explicitly (toolbar toggle or the
    // Appearance row in Default Visuals).
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', e => {
      if (app.accents.themeMode !== 'system') return;
      document.documentElement.setAttribute('data-theme', e.matches ? 'dark' : 'light');
      app.accents.updateThemeIcon();
    });
  }

  wireKeyboard() {
    const app = this.app;
    // Click a control by id only when it exists and isn't disabled (mirrors a real UI click).
    const clickIfActive = id => {
      const el = document.getElementById(id);
      if (!el || el.disabled) return;
      // Alt+<letter> presses Alt first, and Alt over an icon opens its window as a hover-peek
      // (ui/popover.js). Without this the letter would arrive to find the window already open
      // and toggle it straight back shut. Claiming the peek keeps what the user asked for.
      if (el.__stencilGestures?.hotkey?.()) return;
      el.click();
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
        // Route through setImageFilter so the cycle marks the filter dirty + syncs to the server.
        app.settings.setImageFilter(opts[(cur + 1) % opts.length]);
      },
      cycleCompare: () => {
        if (!app.image) return;
        const cur = COMPARE_MODES.indexOf(app.compareMode);
        app.settings.setCompareMode(COMPARE_MODES[(cur + 1) % COMPARE_MODES.length]);
      },
      resetZoom: () => app.zoomPan.fitToWindow(),
      toggleControls: () => { const b = document.getElementById('toggle-controls');   if (b) b.click(); },
      togglePointsList: () => { const b = document.getElementById('toggle-coord-panel'); if (b) b.click(); },
      fullscreen: () => app.toggleFullscreen?.(),
      zoomIn: () => app.zoomPan.zoomAroundCenter(app.scale + 0.25),
      zoomOut: () => app.zoomPan.zoomAroundCenter(app.scale - 0.25),
      zoomInBig: () => app.zoomPan.zoomAroundCenter(app.scale + 1.0),
      zoomOutBig: () => app.zoomPan.zoomAroundCenter(app.scale - 1.0),
      // Alt+R rotates the IMAGE — but with a line selected it instead arms the line-rotate
      // chord (Alt+R+←/→, handled in wireArrowPan), so it must not also spin the image.
      // Deselect to rotate the image again. Alt+Shift+R (right) is unaffected by the chord.
      rotateImageLeft: () => { if (app.image && app.selectedIndices().length === 0) app.imageModel.rotateImage(-1); },
      rotateImageRight: () => { if (app.image) app.imageModel.rotateImage(1); },
      // Alt+Shift+Arrow transforms of the SELECTED line — flip about / rotate ±90 around its bbox
      // centre (same pivot as the arbitrary-angle rotate). The methods no-op without a selection.
      flipLineHorizontal: () => app.flipSelectedLine(true),
      flipLineVertical: () => app.flipSelectedLine(false),
      rotateLineCW90: () => app.rotateSelectedLineQuarter(1),
      rotateLineCCW90: () => app.rotateSelectedLineQuarter(-1),
      copyImage: () => app.export.copyImageToClipboard(),
      copyLayout: () => app.export.copyLayoutToClipboard(),
      // paste is handled by the native 'paste' event listener below — entry here is for hotkey display only
      paste: () => { /* handled by paste event */ },
      clearAllLines: () => app.clearAllLines(),
      // Deletes the WHOLE selection (selectedIndices() falls back to [selectedLineIdx]);
      // on Mac the default reads as ⌥⌫. With a POINT focused, the chord narrows to that
      // point — same route-a-shared-chord-by-selection pattern as Alt+Shift+Arrow.
      deleteLine: () => {
        if (app.isDrawing) return;
        if (app.coordLineIdx >= 0 && app.focusedPtIdx >= 0) {
          app.removePoint(app.coordLineIdx, app.focusedPtIdx);
          return;
        }
        app.removeSelectedLines();
      },
      // Delete the focused point of the selected line (the point, not the whole line).
      deletePoint: () => {
        if (app.isDrawing) return;
        if (app.coordLineIdx >= 0 && app.focusedPtIdx >= 0) app.removePoint(app.coordLineIdx, app.focusedPtIdx);
      },
      // Esc clears the selection — the desktop's "Deselect" action bound to the same key.
      // An open modal owns Escape first: it closes on this very keypress, so also dropping
      // the canvas selection behind it would be an invisible side effect of closing a dialog.
      deselect: () => { if (!document.querySelector('.modal-open')) app.deselectLine(); },
      // Toolbar/menu openers — each just drives the matching button so the shortcut and the
      // click path stay identical (clickIfActive skips a disabled control, like the UI does).
      loadImage: () => clickIfActive(app.image ? 'open-image-btn' : 'load-image-btn'),
      openAnotherImage: () => clickIfActive(app.image ? 'open-image-btn' : 'load-image-btn'),
      openIn: () => clickIfActive('open-in-btn'),
      saveImage: () => clickIfActive('save-image'),
      // Only rendered where the Web Share API takes files (mobile/PWA); exportService
      // guards the unsupported case with a toast, so the chord is safe everywhere.
      shareImage: () => clickIfActive('share-image'),
      cropImage: () => clickIfActive('crop-image'),
      downloadJson: () => clickIfActive('download-json'),
      uploadJson: () => clickIfActive('upload-json-btn'),
      saveProject: () => clickIfActive('save-project-btn'),
      openProject: () => clickIfActive('open-project-btn'),
      toggleLiveSync: () => clickIfActive('live-sync-btn'),
      deleteProject: () => clickIfActive('delete-project-btn'),
      // Remove the CURRENT project from the editor (the trash in Data) — distinct from
      // deleteProject above, which deletes the .stencil file on disk.
      clearProject: () => clickIfActive('clear-storage'),
      // The ✎ next to the project name: enters inline rename (focus + select). A no-op
      // with no saved project, exactly as clicking the pencil is.
      renameProject: () => clickIfActive('project-name-edit'),
      openProjects: () => clickIfActive('projects-btn'),
      openServers: () => clickIfActive('connect-btn'),
      openLinks: () => clickIfActive('links-btn'),
      toggleTheme: () => clickIfActive('theme-toggle'),
      toggleIncognito: () => clickIfActive('incognito-toggle'),
      toggleChat: () => clickIfActive('chat-btn'),
      openHelp: () => clickIfActive('info-btn'),
      openHotkeys: () => clickIfActive('settings-btn'),
      openVisuals: () => clickIfActive('visuals-btn')
    };
    // Editing hotkeys are inert while a compare view is active (it's read-only).
    const EDIT_HOTKEYS = new Set([
      'startDraw', 'stopDraw', 'undo', 'redo', 'clearAllLines', 'deleteLine', 'deletePoint',
      'flipLineHorizontal', 'flipLineVertical', 'rotateLineCW90', 'rotateLineCCW90',
    ]);
    // The Alt+Shift+Arrow line transforms — flip ↑/↓, rotate ±90 ↔ — act on the selection only.
    // ↑/↓ share their combo with big-zoom, so the loop routes the shared chord by selection below.
    const LINE_TRANSFORM_HOTKEYS = new Set(['flipLineHorizontal', 'flipLineVertical', 'rotateLineCW90', 'rotateLineCCW90']);
    document.addEventListener('keydown', e => {
      if (isTypingTarget(e.target)) {
        const id = typingHotkeyId(e, hotkeys);
        if (id) {
          e.preventDefault();
          HK_HANDLERS[id]?.();
        }
        return;
      }
      // Bare Delete / Backspace removes the selected line(s) — what every editor does, and
      // what the coord-table rows already do (coordTable.js). Modifier-free only, so
      // Alt+Delete keeps its own binding and Ctrl/Cmd+Shift+Backspace still deletes the project file.
      if ((e.key === 'Delete' || e.key === 'Backspace') &&
          !e.altKey && !e.ctrlKey && !e.metaKey && !e.shiftKey &&
          !app.isDrawing && !app.compareReadOnly() && app.selectedIndices().length) {
        e.preventDefault();
        app.removeSelectedLines();
        return;
      }
      for (const def of HOTKEY_DEFS) {
        const combo = hotkeys.get(def.id);
        if (!combo) continue;
        if (!matchHotkey(e, combo)) continue;
        // Alt+Shift+↑/↓ big-zooms with nothing selected but flips the SELECTED line otherwise;
        // ↔ rotates it ±90. Route the shared chord by selection: yield big-zoom to the flip
        // binding (later in the registry) when a line is selected, fall through when none is.
        if ((def.id === 'zoomInBig' || def.id === 'zoomOutBig') && app.selectedIndices().length >= 1 && !app.compareReadOnly()) continue;
        if (LINE_TRANSFORM_HOTKEYS.has(def.id) && app.selectedIndices().length === 0) continue;
        // Skip 'paste' here — let the browser fire its native paste event
        if (def.id === 'paste') return;
        // Compare view is read-only — swallow editing shortcuts (but keep view/nav ones).
        if (EDIT_HOTKEYS.has(def.id) && app.compareReadOnly()) { e.preventDefault(); return; }
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

    // Alt+Shift+O — momentary "peek at the original" while held. Physical KeyO (e.code) so
    // it's layout-independent (Mac Option+key produces special e.key chars); not
    // registry-driven because it needs key-up.
    const setHoldOriginal = on => {
      if (app.compareHoldOriginal === on) return;
      app.compareHoldOriginal = on;
      app.renderer.redraw();
      app.updateButtons();   // read-only peek greys the editing controls too
    };
    document.addEventListener('keydown', e => {
      if (e.repeat || isTypingTarget(e.target)) return;
      if (e.code === 'KeyO' && e.altKey && e.shiftKey && !e.ctrlKey && !e.metaKey && app.image) {
        e.preventDefault();
        setHoldOriginal(true);
      }
    });
    document.addEventListener('keyup', e => {
      // Releasing the letter OR any required modifier ends the peek.
      if (app.compareHoldOriginal &&
          (e.code === 'KeyO' || e.key === 'Alt' || e.key === 'Shift' || e.key === 'Meta' || e.key === 'Control'))
        setHoldOriginal(false);
    });
    // A lost focus (window blur / tab switch) never delivers key-up — drop the peek.
    window.addEventListener('blur', () => setHoldOriginal(false));
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
      if (e.key === 'r' || e.key === 'R') this.#rHeld = true;  // track the Alt+R+←/→ chord
      if (e.key === 'Shift') { this.#arrowsHeld.add('Shift'); return; }
      if (!ARROW_KEYS.includes(e.key)) return;

      // Alt+R + ←/→ → rotate the selected line(s) (← CCW, → CW), 3°/press. Takes precedence
      // over pan/zoom so the chord always rotates when a line is selected.
      if (e.altKey && this.#rHeld && (e.key === 'ArrowLeft' || e.key === 'ArrowRight')
          && app.selectedIndices().length >= 1 && !app.compareReadOnly()) {
        e.preventDefault();
        app.rotateSelectedLine((e.key === 'ArrowLeft' ? -1 : 1) * (Math.PI / 60));
        return;
      }
      // Don't steal other Alt/Ctrl/Meta+Arrow combos used by other shortcuts (e.g. zoom).
      if (e.ctrlKey || e.altKey || e.metaKey) return;

      // With a line selected, plain arrows NUDGE the selection (1px, Shift = 10px, in image
      // space); with nothing selected they fall through to panning the viewport. In a compare
      // (read-only) view, nudging is disabled — arrows always pan.
      if (app.selectedIndices().length >= 1 && !app.compareReadOnly()) {
        e.preventDefault();
        const step = e.shiftKey ? 10 : 1;
        let dx = 0, dy = 0;
        if (e.key === 'ArrowLeft') dx = -step;
        else if (e.key === 'ArrowRight') dx = step;
        else if (e.key === 'ArrowUp') dy = -step;
        else if (e.key === 'ArrowDown') dy = step;
        app.nudgeSelected(dx, dy);
        return;
      }

      e.preventDefault();
      this.#arrowsHeld.add(e.key);
      if (!this.#arrowPanRaf) this.#arrowPanRaf = requestAnimationFrame(arrowPanTick);
    });
    document.addEventListener('keyup', e => {
      if (e.key === 'r' || e.key === 'R') this.#rHeld = false;
      if (e.key === 'Shift') this.#arrowsHeld.delete('Shift');
      if (ARROW_KEYS.includes(e.key)) this.#arrowsHeld.delete(e.key);
    });
    // Clear held keys if the window loses focus while arrows are held
    window.addEventListener('blur', () => { this.#arrowsHeld.clear(); this.#rHeld = false; });
  }

  wireDropPaste() {
    const app = this.app;
    // Document-wide drag-and-drop overlay, split into LEFT (upload + save) and RIGHT
    // (upload incognito) zones. The cursor's half of the window decides which.
    const dropZone = document.getElementById('global-drop-overlay');
    const dropLeftHalf = (e) => e.clientX < window.innerWidth / 2;
    const clearZoneCue = () => dropZone.querySelectorAll('.drop-zone-active').forEach((z) => z.classList.remove('drop-zone-active'));

    // An image dragged from ANOTHER web page arrives as a URL, not a File (see dragImageUrl.js).
    const draggedImageUrl = (dt) => extractDraggedImageUrl((t) => dt.getData(t));
    // Fetch a dragged image URL into a File so it flows through the same load path as a
    // dropped file (shared with the chat's drop-to-attach — dragImageUrl.js).
    const fetchUrlToFile = (url) => fetchDraggedMediaFile(url);

    // Load a dropped image via the LEFT (save) or RIGHT (incognito) zone; with an image
    // open, first ask current page vs new one. `from` = the drop point in client coords —
    // the canvas plays in out of it (ui/motion.js arriveFrom); dialog/paste paths have none.
    const handleImageDrop = async (file, incognito, from = null) => {
      if (app.image) {
        // Three BUTTONS, not a picker: two answers and a way out, each one click. Desktop
        // parity — mainWindow.cpp asks the same question with This window / New window / Cancel.
        const where = await app.askAlt('An image is already open. Where should the dropped image open?', {
          title: 'Open dropped image',
          confirmLabel: 'Open in the current page', confirmIcon: 'image',
          altLabel: 'Open in a new page', altIcon: 'external',
        });
        if (!where) return;                                                    // Cancel / Escape
        if (where === 'alt') { app.openImageNewTab(file, incognito); return; }
      }
      // `landing` = play the canvas reveal — this image arrived by drop, not by dialog.
      app.openImageHere(file, incognito, null, { landing: true, from });
    };

    // Internal row-reorder drags (Servers / Projects modals) carry this flag; the image-drop
    // overlay must ignore them (a connection row's URL would otherwise pop the overlay + try to
    // fetch it as an image on drop).
    const isReorderDrag = (e) => { try { return e.dataTransfer.types.includes('application/x-stencil-reorder'); } catch { return false; } };

    // Drops on an element that wires its OWN drop handlers belong to it — the overlay
    // would cover it and swallow its events, so it stands down over its rect. Owners
    // declare themselves with [data-drop-owner] while visible (today: the open chat panel).
    const overDropOwner = (x, y) => {
      for (const el of document.querySelectorAll('[data-drop-owner]')) {
        if (pointInRect(x, y, el.getBoundingClientRect())) return true;
      }
      return false;
    };

    document.addEventListener('dragenter', e => {
      e.preventDefault();
      if (isReorderDrag(e)) return;   // internal row reorder — not an image drop
      if (overDropOwner(e.clientX, e.clientY)) { hideDropOverlay(dropZone); clearZoneCue(); return; }
      // Show the overlay for a dragged File OR an image dragged from another page (uri-list /
      // html — a URL, not a File). Plain text alone isn't treated as a drop (too noisy).
      const t = e.dataTransfer.types;
      if (t.includes('Files') || t.includes('text/uri-list') || t.includes('text/html')) showDropOverlay(dropZone);
    });
    document.addEventListener('dragover', e => {
      e.preventDefault();
      if (isReorderDrag(e)) return;   // internal row reorder — leave it to the modal's own handlers
      // Over a drop owner (the open chat panel): hide the overlay so it gets the drop.
      if (overDropOwner(e.clientX, e.clientY)) { hideDropOverlay(dropZone); clearZoneCue(); return; }
      e.dataTransfer.dropEffect = 'copy';
      // Highlight the half the cursor is over so the save/incognito choice is legible.
      if (dropZone.style.display !== 'none' && dropZone.style.display !== '') {
        const left = dropLeftHalf(e);
        const lz = dropZone.querySelector('.drop-zone-left');
        const rz = dropZone.querySelector('.drop-zone-right');
        if (lz) lz.classList.toggle('drop-zone-active', left);
        if (rz) rz.classList.toggle('drop-zone-active', !left);
      }
    });
    document.addEventListener('dragleave', e => {
      // Only hide when leaving the entire window
      if (e.relatedTarget === null) { hideDropOverlay(dropZone); clearZoneCue(); }
    });
    // A drag that ends without a drop landing here (cancelled, or claimed by an
    // overlay that swallowed the events) must never strand the zones on screen.
    document.addEventListener('dragend', () => { hideDropOverlay(dropZone); clearZoneCue(); });
    document.addEventListener('drop', e => {
      e.preventDefault();
      hideDropOverlay(dropZone);
      clearZoneCue();
      if (isReorderDrag(e)) return;   // internal row reorder — don't try to load an image
      if (overDropOwner(e.clientX, e.clientY)) return;   // the owner's (chat panel's) drop handler owns it
      const incognito = !dropLeftHalf(e);   // RIGHT half = incognito, LEFT half = upload + save
      const from = { x: e.clientX, y: e.clientY };
      const file = e.dataTransfer.files[0];
      if (!file) {
        // No File → maybe an image dragged from another page (a URL). Fetch it into a File.
        const url = draggedImageUrl(e.dataTransfer);
        if (!url) {
          // A relative <img src> carries no origin, so there is nothing to fetch —
          // say so instead of silently doing nothing (dragImageUrl.js absolutize).
          notify('Could not read an image URL from that drag — try dragging the image from its own page, or copy the image address and use Open image → URL', 'fail');
          return;
        }
        // Surface the real failure (bad URL, 404, non-image response) instead of
        // blaming CORS unconditionally.
        fetchUrlToFile(url)
          .then((f) => handleImageDrop(f, incognito, from))
          .catch((err) => notify(`Could not load the dragged image — ${err.message}. `
            + 'If the site blocks cross-origin downloads, try the extension or desktop app.', 'fail'));
        return;
      }
      if (file.name.endsWith('.stencil')) {
        app.export.openProjectFile(file, { from });   // a whole .stencil project ignores the save/incognito split
      } else if (file.type.startsWith('image/')) {
        handleImageDrop(file, incognito, from);
      } else if (file.name.endsWith('.json') || file.type === 'application/json') {
        app.loadJSONFromFile(file, { from });   // a .json layout ignores the save/incognito split
      } else {
        notify('Please drop an image, a .json layout, or a .stencil project', 'fail');
      }
    });

    // Clipboard paste (Ctrl+V) — handles images and JSON layout text
    document.addEventListener('paste', async e => {
      if (isTypingTarget(e.target)) return; // let native paste work in inputs
      const cd = e.clipboardData;
      if (!cd) return;

      // 1) Image takes priority. mediaFilesFromData reads the items SYNCHRONOUSLY
      // (clipboardData is invalid after an await); the chat panel shares it for its
      // own attach-on-paste, which stops propagation before this handler runs.
      const hasImageItem = [...cd.items].some((item) => item.type && item.type.startsWith('image/'));
      if (hasImageItem) {
        e.preventDefault();
        const file = mediaFilesFromData(cd).find((f) => f.type.startsWith('image/'));
        if (app.image && !(await app.confirm('Replace current image with pasted image?', { title: 'Replace image', confirmIcon: 'paste' }))) {
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
    // The <canvas> covers only the image; clicking the letterbox padding around it is
    // still "clicking empty space" and must clear the selection. Fires only when the click
    // landed on the container itself, so canvas clicks keep flowing through canvasClick().
    const emptyArea = document.getElementById('canvas-viewport');
    if (emptyArea) {
      emptyArea.addEventListener('click', (e) => {
        if (e.target !== e.currentTarget && e.target.id !== 'canvas-container') return;
        app.deselectEmptyArea(e);
      });
      // Hand focus to the canvas, else it stays on the chat textarea (focused when the panel
      // opens) and a bare Delete edits chat text instead of the selected line. On the viewport
      // so the letterbox counts, on pointerdown for pen/touch too, preventScroll to not jump.
      emptyArea.addEventListener('pointerdown', () => {
        if (document.activeElement !== app.canvas) app.canvas.focus({ preventScroll: true });
      });
    }
    app.canvas.addEventListener('dblclick', e => app.canvasDblClick(e));
    app.canvas.addEventListener('mousemove', e => app.canvasMouseMove(e));
    app.canvas.addEventListener('mouseleave', () => {
      app.mouseOverCanvas = false;
      app.tooltipMgr.hide();
      app.updateCoordStatus();
      if (app.hoverPt) { app.hoverPt = null; app.renderer.redraw(); }
      if (app.hoverLineIdx !== -1) { app.hoverLineIdx = -1; app.applyLinesListHover(); }
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
          const org = app.zoomPan.originAt(sz.target);
          viewport.scrollLeft = imgX * sz.target + org.x - clientX;
          viewport.scrollTop = imgY * sz.target + org.y - clientY;
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
      // No viewport resize per frame: the frame is full-height at every zoom now.
      app.zoomPan.setZoomInputValue(Math.round(next * 100));

      // Maintain focal point: keep the image pixel under the cursor fixed. Through the
      // centring margins at THIS scale (originAt), which are what hold a picture smaller
      // than the frame in the middle — they collapse to 0 exactly when scrolling starts,
      // so the pixel stays put across that boundary instead of jumping.
      if (sz.focal && viewport) {
        const { imgX, imgY, clientX, clientY } = sz.focal;
        const org = app.zoomPan.originAt(next);
        viewport.scrollLeft = imgX * next + org.x - clientX;
        viewport.scrollTop = imgY * next + org.y - clientY;
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
      // (point's line if hovering a point, else the hovered line). Read-only while comparing.
      if (e.altKey && !e.ctrlKey && !e.metaKey && !app.compareReadOnly()) {
        e.preventDefault();
        app.adjustThicknessAtCursor(e);
        return;
      }

      // Ctrl+Shift+wheel with a selection → rotate it. One line: around its centre (or the focused
      // point). Multiple lines: all together around their combined centre. Read-only while comparing.
      if ((e.ctrlKey || e.metaKey) && e.shiftKey && app.selectedIndices().length >= 1 && !app.compareReadOnly()) {
        e.preventDefault();
        const dir = e.deltaY > 0 ? 1 : -1;
        app.rotateSelectedLine(dir * (Math.PI / 60)); // 3° per tick
        return;
      }

      e.preventDefault();

      // Zoom increment scaled by the wheel delta (not a fixed step): a mouse notch steps a
      // sensible amount while touchpad-pinch micro-deltas sum gently. deltaMode normalizes
      // line/page units to pixels; the cap keeps a big notch from overshooting.
      const px = e.deltaY * (e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? 400 : 1);
      const factor = e.shiftKey ? 0.0095 : 0.004;
      const cap = e.shiftKey ? 0.65 : 0.32;
      const delta = Math.max(-cap, Math.min(cap, -px * factor));
      const sz = this.#smoothZoom;

      // Accumulate target scale (clampScale = the shared [0.05, kZoomMax] zoom bound)
      const base = sz.target !== null ? sz.target : app.scale;
      sz.target = app.zoomPan.clampScale(base + delta);

      // Focal point in image-space (unscaled pixels).
      // Works in both fullscreen (viewport fixed at 0,0) and normal mode.
      // Through the centring margins (canvasOrigin): with the picture smaller than the
      // frame the scroll origin is not the image origin, and the cursor would pin the
      // wrong pixel the moment the zoom grew past the frame. The scroll writes in
      // runSmoothZoom put the margin back on, at the scale of that frame.
      const vpRect = viewport.getBoundingClientRect();
      const org = canvasOrigin();
      const contentX = e.clientX - vpRect.left + viewport.scrollLeft;
      const contentY = e.clientY - vpRect.top  + viewport.scrollTop;
      sz.focal = {
        imgX: (contentX - org.x) / app.scale,
        imgY: (contentY - org.y) / app.scale,
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
