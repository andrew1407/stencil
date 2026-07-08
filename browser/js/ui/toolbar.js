import { StencilElement, hostTag, define } from './base.js';
import { DRAW_MODE_ICON } from '../core/drawingApp.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
import { ACCENTS, DEFAULT_ACCENT, accentHex, normalizeHex } from '../core/accents.js';
import { pageFormatOptions } from '../core/units.js';
// ── Component: toolbar (controls-wrapper + all 8 sections) ──────
// Owns the controls markup and the collapse/hints behavior. The individual
// inputs/buttons are wired by DrawingApp via global ids.
export class StencilToolbar extends StencilElement {
  static inner() {
    return `
            <div class="controls-topbar">
                <svg class="app-logo" viewBox="0 0 64 64" width="24" height="24" role="img" aria-label="Stencil" focusable="false">
                    <rect x="2" y="2" width="60" height="60" rx="13" fill="#2b2f3a"/>
                    <rect class="app-logo-frame" x="2.75" y="2.75" width="58.5" height="58.5" rx="12.25" fill="none" stroke-width="2.5"/>
                    <rect x="12" y="12" width="40" height="40" rx="4" fill="#3a3f4b"/>
                    <polyline points="16,46 27,24 38,38 50,18" fill="none" stroke="#FFFF00" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>
                    <g fill="#FFFF00" stroke="#000000" stroke-width="1.25">
                        <circle cx="16" cy="46" r="3.4"/><circle cx="27" cy="24" r="3.4"/><circle cx="38" cy="38" r="3.4"/><circle cx="50" cy="18" r="3.4"/>
                    </g>
                </svg>
                <button id="toggle-controls" class="btn-icon-text" data-hk-title="toggleControls" data-title="Hide controls" title="Hide controls">${icon('chevron-up')}<span>Controls</span></button>
                <span class="project-name-field" style="flex:0 1 240px;min-width:90px;display:inline-flex;align-items:center;gap:4px;">
                    <span id="project-remote-badge" class="project-remote-badge" style="display:none;flex:0 0 auto;" title="Editing a project stored on a server">${icon('server', { size: 13 })}</span>
                    <input id="project-name-input" type="text" placeholder="No project" readonly disabled
                        style="flex:1 1 auto;min-width:0;font-size:13px;font-weight:600;background:transparent;border:1px solid transparent;border-radius:6px;padding:3px 8px;">
                    <button id="project-name-edit" class="name-edit-btn name-edit-pencil" type="button" title="Rename project" style="display:none;">${icon('pencil', { size: 13 })}</button>
                    <button id="project-name-accept" class="name-edit-btn name-edit-accept" type="button" title="Save name" style="display:none;">${icon('check', { size: 14 })}</button>
                    <button id="project-name-cancel" class="name-edit-btn name-edit-cancel" type="button" title="Cancel" style="display:none;">${icon('x', { size: 14 })}</button>
                    <button id="project-color-btn" class="name-edit-btn" type="button" title="Project colour — paints the project name" style="display:none;">${icon('palette', { size: 14 })}</button>
                    <input id="project-color-input" type="color" tabindex="-1" aria-hidden="true" style="position:absolute;width:1px;height:1px;opacity:0;border:0;padding:0;pointer-events:none;">
                </span>
                <span id="hints-btn" style="display:none;position:relative;cursor:default;font-size:12px;color:var(--text-muted);border:1px solid var(--border-main);border-radius:12px;padding:2px 8px;user-select:none;">
                    ?
                    <span class="hints-popup" id="hints-popup"></span>
                </span>
            </div>
            <div id="controls-body">
        <div class="controls">

            <!-- ── Section: Image ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Image</div>
                <div class="ctrl-section-row">
                    <!-- One Open entry (empty state). Opens the unified Open dialog: local file, URL, or new blank. -->
                    <button id="load-image-btn" class="btn-icon-text" data-hk-title="loadImage" data-title="Open an image — local file, URL, or new blank" title="Open an image — local file, URL, or new blank">${icon('image')}<span>Open Image</span></button>
                    <!-- Image actions (shown only when an image is loaded). #save-image moved here from Data. -->
                    <span id="image-actions" style="display:none;align-items:center;gap:4px;">
                        <button id="save-image" class="btn-icon" data-hk-title="saveImage" data-title="Download image" data-disabled-reason="Load an image to download it" title="Download image">${icon('download')}</button>
                        <button id="copy-image" class="btn-icon" data-hk-title="copyImage" data-title="Copy image to clipboard" data-disabled-reason="Load an image to copy it" title="Copy image to clipboard">${icon('copy')}</button>
                        <button id="share-image" class="btn-icon" data-title="Share image" title="Share image" style="display:none;">${icon('share')}</button>
                        <button id="open-in-btn" class="btn-icon" data-hk-title="openIn" data-title="Open in another app" title="Open in another app">${icon('monitor')}</button>
                        <button id="open-image-btn" class="btn-icon" data-hk-title="openAnotherImage" data-title="Open another image — local file, URL, or new blank" title="Open another image — local file, URL, or new blank">${icon('external')}</button>
                    </span>
                    <span id="image-size-display" style="display:none;font-size:12px;color:var(--text-muted);background:var(--bg-info);padding:3px 8px;border-radius:4px;border:1px solid var(--border-main);white-space:nowrap;"></span>
                    <!-- Blank-image fill colour: a swatch next to the size pill, shown only for blank projects. Click to recolour the background (lines are kept). -->
                    <button id="blank-color-btn" type="button" title="Blank background colour — recolour this blank image (keeps your lines)" style="display:none;align-items:center;gap:6px;font-size:12px;color:var(--text-muted);background:var(--bg-info);padding:3px 8px;border-radius:4px;border:1px solid var(--border-main);white-space:nowrap;cursor:pointer;">
                        <span id="blank-color-swatch" style="width:13px;height:13px;border-radius:3px;border:1px solid var(--border-main);display:inline-block;flex:0 0 auto;"></span>Blank
                    </button>
                    <input id="blank-color-input" type="color" tabindex="-1" aria-hidden="true" style="position:absolute;width:1px;height:1px;opacity:0;border:0;padding:0;pointer-events:none;">
                    <select id="image-filter" data-hk-title="cycleFilter" data-title="Image Filter" data-disabled-reason="Load an image to apply a filter" title="Image Filter">
                        <option value="none">No Filter</option>
                        <option value="bw">B&amp;W</option>
                        <option value="sepia">Sepia</option>
                        <option value="invert">Invert</option>
                        <option value="contour">Contour</option>
                        <option value="custom">Tint</option>
                    </select>
                    <input type="color" id="filter-color" value="#7c3aed" title="Tint color" style="display:none;width:36px;height:30px;padding:2px;cursor:pointer;border-radius:4px;">
                    <button id="crop-image" class="btn-icon-text" data-hk-title="cropImage" data-title="Crop image" data-disabled-reason="Load an image to crop" title="Crop image — pick the page-shaped region to show on the canvas">${icon('crop')}<span>Crop</span></button>
                    <button id="rotate-left" class="btn-icon" data-hk-title="rotateImageLeft" data-title="Rotate image left" data-disabled-reason="Load an image to rotate" title="Rotate image left">${icon('rotate-ccw')}</button>
                    <button id="rotate-right" class="btn-icon" data-hk-title="rotateImageRight" data-title="Rotate image right" data-disabled-reason="Load an image to rotate" title="Rotate image right">${icon('rotate-cw')}</button>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Drawing style ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Line Style</div>
                <div class="ctrl-section-row">
                    <input type="color" id="line-color" value="#FFFF00" title="Line color">
                    <input type="number" id="line-thickness" value="2" min="1" max="20" title="Line thickness" style="width:54px">
                    <input type="number" id="marker-size" value="4" min="1" max="30" title="Marker size" style="width:54px">
                    <select id="line-style" title="Line style">
                        <option value="solid">Solid</option>
                        <option value="dashed">Dashed</option>
                        <option value="dotted">Dotted</option>
                    </select>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Drawing actions ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Draw</div>
                <div class="ctrl-section-row">
                    <button id="start-drawing" class="btn-icon-text" data-hk-title="startDraw" data-title="Start Drawing" data-disabled-reason="Load an image to start drawing" title="Start Drawing">${icon('play', { size: 13 })}<span>Start</span></button>
                    <button id="stop-drawing" disabled class="btn-icon-text" data-hk-title="stopDraw" data-title="Stop Drawing" data-disabled-reason="Start drawing first" title="Stop Drawing">${icon('stop', { size: 13 })}<span>Stop</span></button>
                    <button id="draw-mode-toggle" class="btn-icon-text" data-title="Drawing mode: Line" data-disabled-reason="Load an image to switch line / rectangle" title="Drawing mode: Line (click to switch to Rectangle)">${DRAW_MODE_ICON.line}<span>Line</span></button>
                    <button id="undo" disabled class="btn-icon" data-hk-title="undo" data-title="Undo" data-disabled-reason="Nothing to undo" title="Undo">${icon('undo')}</button>
                    <button id="redo" disabled class="btn-icon" data-hk-title="redo" data-title="Redo" data-disabled-reason="Nothing to redo" title="Redo">${icon('redo')}</button>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: View ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">View</div>
                <div class="ctrl-section-row">
                    <label data-hk-title="togglePoints" style="font-weight:normal;font-size:13px;cursor:pointer;display:flex;align-items:center;gap:4px;" title="Show Points (Alt+P)">
                        <input type="checkbox" id="show-points" checked> Points
                    </label>
                    <label data-hk-title="toggleLines" style="font-weight:normal;font-size:13px;cursor:pointer;display:flex;align-items:center;gap:4px;" title="Show Lines (Alt+L)">
                        <input type="checkbox" id="show-lines" checked> Lines
                    </label>
                    <button id="clear-all-lines" class="danger btn-icon-text" data-hk-title="clearAllLines" data-title="Clear All Lines" data-disabled-reason="No lines to clear" title="Clear All Lines">${icon('trash')}<span>Clear</span></button>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Zoom ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Zoom</div>
                <div class="ctrl-section-row">
                    <div class="zoom-controls">
                        <button id="zoom-out" class="btn-icon" data-title="Zoom out" data-disabled-reason="Load an image to zoom" title="Zoom out">${icon('minus')}</button>
                        <button id="zoom-in" class="btn-icon" data-title="Zoom in" data-disabled-reason="Load an image to zoom" title="Zoom in">${icon('plus')}</button>
                        <input type="number" id="zoom-input" value="100" min="5" max="500" data-title="Zoom %" data-disabled-reason="Load an image to zoom" title="Zoom % (Enter to apply)">
                        <span style="font-size:13px;font-weight:bold;color:var(--text-muted)">%</span>
                        <button id="zoom-fit" class="btn-icon" data-hk-title="resetZoom" data-title="Fit to window" data-disabled-reason="Load an image to zoom" title="Fit to window">${icon('fit')}</button>
                    </div>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Page ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Page</div>
                <div class="ctrl-section-row">
                    <!-- Custom… first, then every named ISO format from PAGE_SIZES with its
                         physical size (re-rendered in the active unit by applyUnitToUI). -->
                    <select id="page-size" title="Page size">
                        <option value="custom">Custom…</option>
                        ${pageFormatOptions()}
                    </select>
                    <label style="font-weight:normal;font-size:12px;color:var(--text-muted);">Units:</label>
                    <select id="unit-select" title="Display units (cm / inches)">
                        <option value="cm">cm</option>
                        <option value="in">in</option>
                    </select>
                    <span id="custom-size-group" style="display:none;align-items:center;gap:6px;">
                        <label style="font-weight:normal;font-size:12px;color:var(--text-muted);">W</label>
                        <input type="number" id="custom-page-width" value="21" min="0.1" max="500" step="0.1" style="width:96px">
                        <label style="font-weight:normal;font-size:12px;color:var(--text-muted);">H</label>
                        <input type="number" id="custom-page-height" value="29.7" min="0.1" max="500" step="0.1" style="width:96px">
                        <span id="custom-unit-label" style="font-size:12px;color:var(--text-muted);">cm</span>
                    </span>
                    <label style="font-weight:normal;font-size:13px;cursor:pointer;display:flex;align-items:center;gap:5px;margin-left:6px;">
                        <input type="checkbox" id="allow-formulas"> 𝑓(x,y)
                    </label>
                    <span id="formula-inputs" style="display:none;align-items:center;gap:6px;">
                        <input type="text" id="formula-x" placeholder="x(x)=" style="width:180px;font-family:monospace;font-size:12px;">
                        <input type="text" id="formula-y" placeholder="y(y)=" style="width:180px;font-family:monospace;font-size:12px;">
                        <span id="formula-error" title="Invalid formula" style="color:var(--danger);display:none;">${icon('alert', { size: 15 })}</span>
                    </span>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Data ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Data</div>
                <div class="ctrl-section-row">
                    <button id="download-json" class="btn-icon-text" data-hk-title="downloadJson" data-title="Download Layout JSON" data-disabled-reason="Draw at least one line to export" title="Download Layout JSON">${icon('download')}<span>JSON</span></button>
                    <button id="copy-json-btn" class="btn-icon" data-hk-title="copyLayout" data-title="Copy full Layout JSON (lines + all applied edits)" data-disabled-reason="Draw at least one line to copy" title="Copy full Layout JSON (lines + all applied edits)">${icon('copy')}</button>
                    <input type="file" id="upload-json" accept=".json" style="display:none;">
                    <button id="upload-json-btn" class="btn-icon" data-hk-title="uploadJson" data-title="Upload Layout JSON" title="Upload Layout JSON">${icon('upload')}</button>
                    <button id="clear-storage" class="danger btn-icon" data-title="Clear saved storage" title="Clear saved storage">${icon('trash')}</button>
                    <span id="save-status" style="font-size:12px;color:#555;min-width:70px;"></span>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: App ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">App</div>
                <div class="ctrl-section-row">
                    <button id="theme-toggle" class="btn-icon" data-hk-title="toggleTheme" data-title="Toggle dark / light theme" title="Toggle dark / light theme">${icon('moon')}</button>
                    <button id="fullscreen-toggle" class="btn-icon" data-hk-title="fullscreen" data-title="Fullscreen" data-disabled-reason="Load an image to view fullscreen" title="Fullscreen">${icon('maximize')}</button>
                    <button id="projects-btn" class="btn-icon" data-hk-title="openProjects" data-title="Projects" title="Projects">${icon('layers')}</button>
                    <button id="connect-btn" class="btn-icon" data-hk-title="openServers" data-title="Servers — connect to share &amp; co-edit projects" title="Servers — connect to share &amp; co-edit projects">${icon('server')}</button>
                    <button id="links-btn" class="btn-icon" data-hk-title="openLinks" data-title="Source &amp; resource links for the current image" data-disabled-reason="Open an image first to edit its links" title="Source &amp; resource links for the current image">${icon('link')}</button>
                    <button id="incognito-toggle" class="btn-icon" data-hk-title="toggleIncognito" data-title="Incognito — edit without saving (choose before adding an image)" title="Incognito — edit without saving (choose before adding an image)">${icon('incognito')}</button>
                    <button id="settings-btn" class="btn-icon" data-title="Keyboard shortcuts" title="Keyboard shortcuts">${icon('gear')}</button>
                    <button id="visuals-btn" class="btn-icon" data-title="Default visuals &amp; highlight styles" title="Default visuals &amp; highlight styles">${icon('palette')}</button>
                    <button id="info-btn" class="btn-icon" data-hk-title="openHelp" data-title="Controls &amp; shortcuts help" title="Controls &amp; shortcuts help">${icon('help')}</button>
                </div>
            </div>

        </div>
            </div><!-- /controlsBody -->
    `;
  }
  static template() { return hostTag('stencil-toolbar', 'class="controls-wrapper"', StencilToolbar.inner()); }

  wire(_app) {
    const btn = document.getElementById('toggle-controls');
    const body = document.getElementById('controls-body');
    const hintsBtn = document.getElementById('hints-btn');
    const popup = document.getElementById('hints-popup');
    let hidden = false;

    // The image-size bar (#image-info) shows ONLY the size; the shortcut hints live here, in the "?"
    // popup that appears when Controls is collapsed (so the toolbar's shortcuts stay discoverable).
    const SHORTCUTS_HINT =
      'Zoom: Ctrl+Scroll · Alt+± · +/− btn  (+Shift = larger)  |  Alt+Scroll: thickness  |  ' +
      'Ctrl+Shift+Scroll: rotate selected  |  Ctrl+Click: add point  |  ℹ for full help';
    const infoText = () => {
      const el = document.getElementById('image-info');
      const size = el ? el.textContent : 'Image Size: —';
      return `${size}  |  ${SHORTCUTS_HINT}`;
    };

    btn.addEventListener('click', () => {
      hidden = !hidden;
      body.classList.toggle('hidden', hidden);
      btn.innerHTML = (hidden ? icon('chevron-down') : icon('chevron-up')) + '<span>Controls</span>';
      btn.dataset.title = hidden ? 'Show controls' : 'Hide controls';
      btn.title = hotkeys.hkTitle(hidden ? 'Show controls' : 'Hide controls', 'toggleControls');
      hintsBtn.style.display = hidden ? 'inline-block' : 'none';
      hintsBtn.title = SHORTCUTS_HINT;
      if (hidden) popup.textContent = infoText();
    });

    // Keep popup live when image is loaded
    new MutationObserver(() => {
      if (hidden) popup.textContent = infoText();
    }).observe(document.getElementById('image-info'), { childList: true, characterData: true, subtree: true });

    wireLogoColorPicker(this.querySelector('.app-logo'), _app);
  }
}

// Double-click (or double-tap) the logo to open a native colour picker that tints THIS
// page's accent only — not saved, not synced to other windows, gone on reload. Picking a
// preset in the Visuals modal later clears it (see DrawingApp#applyAccent).
function wireLogoColorPicker(logo, app) {
  if (!logo || !app) return;
  logo.style.cursor = 'pointer';
  logo.setAttribute('title', 'Click to cycle the theme colour · double-click for a custom colour');

  // Single-click cycles the MAIN theme accent to the next preset in the ACCENTS order (wrapping).
  // When a CUSTOM (non-preset) colour is currently active — set via the double-click picker — a
  // click instead resets to the default preset (violet). Persisted via app.setAccent, so it's the
  // real theme change. The action is deferred briefly so a double-click cancels it (opens the
  // picker) instead.
  const cycleAccent = () => {
    if (app.customAccent) { app.setAccent(DEFAULT_ACCENT); return; }
    const keys = ACCENTS.map((a) => a.key);
    const i = keys.indexOf(app.accent);
    app.setAccent(keys[(i + 1) % keys.length]);
  };
  let clickTimer = null;
  logo.addEventListener('click', () => {
    if (clickTimer) return;   // second click of a dbl — let dblclick handle it
    clickTimer = setTimeout(() => { clickTimer = null; cycleAccent(); }, 220);
  });

  // A tiny, near-invisible colour input parked under the logo. It stays in normal flow
  // (not display:none / zero-size) so the browser will actually render its native picker.
  const picker = document.createElement('input');
  picker.type = 'color';
  picker.setAttribute('aria-hidden', 'true');
  picker.tabIndex = -1;
  picker.style.cssText = 'position:absolute;width:1px;height:1px;opacity:0;border:0;padding:0;pointer-events:none;';
  logo.insertAdjacentElement('afterend', picker);

  const apply = () => app.setCustomAccent(picker.value); // native colour input yields #rrggbb
  picker.addEventListener('input', apply);   // live while dragging
  picker.addEventListener('change', apply);  // final commit

  const open = () => {
    const cur = app.customAccent || getComputedStyle(document.documentElement).getPropertyValue('--accent');
    picker.value = normalizeHex(cur) || accentHex(app.accent);
    // showPicker() is the reliable way to open a picker programmatically (a bare .click()
    // on a hidden input often won't); fall back to click() on older browsers.
    try {
      if (typeof picker.showPicker === 'function') picker.showPicker();
      else picker.click();
    } catch {
      picker.click();
    }
  };
  logo.addEventListener('dblclick', () => {
    if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }   // cancel the single-click cycle
    open();
  });
  // A double-click selects nearby text; clear it so the picker isn't fighting a selection.
  logo.addEventListener('mousedown', (e) => { if (e.detail > 1) e.preventDefault(); });

  // dblclick is unreliable on touch — detect a double-tap by hand.
  let lastTap = 0;
  logo.addEventListener('touchend', (e) => {
    const now = Date.now();
    if (now - lastTap < 400) {
      e.preventDefault();
      lastTap = 0;
      open();
    } else {
      lastTap = now;
    }
  });
}

define('stencil-toolbar', StencilToolbar);
