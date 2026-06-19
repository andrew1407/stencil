import { StencilElement, hostTag, define } from './base.js';
import { DRAW_MODE_ICON } from '../core/drawingApp.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
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
                    <input id="project-name-input" type="text" placeholder="No project" title="Project name — double-click to rename" readonly disabled
                        style="flex:1 1 auto;min-width:0;font-size:13px;font-weight:600;color:var(--text);background:transparent;border:1px solid transparent;border-radius:6px;padding:3px 8px;">
                    <button id="project-name-edit" class="name-edit-btn name-edit-pencil" type="button" title="Rename project" style="display:none;">${icon('pencil', { size: 13 })}</button>
                    <button id="project-name-accept" class="name-edit-btn name-edit-accept" type="button" title="Save name" style="display:none;">${icon('check', { size: 14 })}</button>
                    <button id="project-name-cancel" class="name-edit-btn name-edit-cancel" type="button" title="Cancel" style="display:none;">${icon('x', { size: 14 })}</button>
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
                    <input type="file" id="image-upload" accept="image/*">
                    <span id="image-size-display" style="display:none;font-size:12px;color:var(--text-muted);background:var(--bg-info);padding:3px 8px;border-radius:4px;border:1px solid var(--border-main);white-space:nowrap;"></span>
                    <select id="image-filter" data-hk-title="cycleFilter" data-title="Image Filter" data-disabled-reason="Load an image to apply a filter" title="Image Filter">
                        <option value="none">No Filter</option>
                        <option value="bw">B&amp;W</option>
                        <option value="sepia">Sepia</option>
                        <option value="custom">Tint</option>
                    </select>
                    <input type="color" id="filter-color" value="#7c3aed" title="Tint color" style="display:none;width:36px;height:30px;padding:2px;cursor:pointer;border-radius:4px;">
                    <button id="crop-image" class="btn-icon-text" data-title="Crop image" data-disabled-reason="Load an image to crop" title="Crop image — pick the page-shaped region to show on the canvas">${icon('crop')}<span>Crop</span></button>
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
                        <input type="number" id="zoom-input" value="100" min="5" max="500" data-title="Zoom %" data-disabled-reason="Load an image to zoom" title="Zoom % (Enter to apply)">
                        <span style="font-size:13px;font-weight:bold;color:var(--text-muted)">%</span>
                        <button id="zoom-in" class="btn-icon" data-title="Zoom in" data-disabled-reason="Load an image to zoom" title="Zoom in">${icon('plus')}</button>
                        <button id="zoom-fit" class="btn-icon" data-hk-title="resetZoom" data-title="Fit to window" data-disabled-reason="Load an image to zoom" title="Fit to window">${icon('fit')}</button>
                    </div>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Page ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Page</div>
                <div class="ctrl-section-row">
                    <select id="page-size" title="Page size">
                        <option value="A3">A3</option>
                        <option value="A4">A4</option>
                        <option value="custom">Custom…</option>
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
                    <button id="download-json" class="btn-icon-text" data-title="Download Layout JSON" data-disabled-reason="Draw at least one line to export" title="Download Layout JSON">${icon('download')}<span>JSON</span></button>
                    <button id="copy-json-btn" class="btn-icon" data-hk-title="copyLayout" data-title="Copy Layout JSON" data-disabled-reason="Draw at least one line to copy" title="Copy Layout JSON">${icon('copy')}</button>
                    <button id="save-image" class="btn-icon" data-title="Save Image" data-disabled-reason="Load an image to save it" title="Save Image">${icon('save')}</button>
                    <input type="file" id="upload-json" accept=".json" style="display:none;">
                    <button id="upload-json-btn" class="btn-icon" title="Upload Layout JSON">${icon('upload')}</button>
                    <button id="clear-storage" class="danger btn-icon" title="Clear saved storage">${icon('trash')}</button>
                    <span id="save-status" style="font-size:12px;color:#555;min-width:70px;"></span>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: App ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">App</div>
                <div class="ctrl-section-row">
                    <button id="theme-toggle" class="btn-icon" data-title="Toggle dark / light theme" title="Toggle dark / light theme">${icon('moon')}</button>
                    <button id="fullscreen-toggle" class="btn-icon" data-hk-title="fullscreen" data-title="Fullscreen" data-disabled-reason="Load an image to view fullscreen" title="Fullscreen">${icon('maximize')}</button>
                    <button id="projects-btn" class="btn-icon" title="Projects">${icon('layers')}</button>
                    <button id="links-btn" class="btn-icon" title="Source &amp; resource links · add image by URL">${icon('link')}</button>
                    <button id="incognito-toggle" class="btn-icon" title="Incognito — edit without saving (choose before adding an image)">${icon('incognito')}</button>
                    <button id="settings-btn" class="btn-icon" title="Keyboard shortcuts">${icon('gear')}</button>
                    <button id="visuals-btn" class="btn-icon" title="Default visuals &amp; highlight styles">${icon('palette')}</button>
                    <button id="info-btn" class="btn-icon" title="Controls &amp; shortcuts help">${icon('help')}</button>
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

    const infoText = () => {
      const el = document.getElementById('image-info');
      return el ? el.textContent : 'Image Size: — | Zoom: Ctrl+Scroll or Alt+Scroll · +/− · ⊡ Fit | Pan: Alt+Drag';
    };

    btn.addEventListener('click', () => {
      hidden = !hidden;
      body.classList.toggle('hidden', hidden);
      btn.innerHTML = (hidden ? icon('chevron-down') : icon('chevron-up')) + '<span>Controls</span>';
      btn.dataset.title = hidden ? 'Show controls' : 'Hide controls';
      btn.title = hotkeys.hkTitle(hidden ? 'Show controls' : 'Hide controls', 'toggleControls');
      hintsBtn.style.display = hidden ? 'inline-block' : 'none';
      if (hidden) popup.textContent = infoText();
    });

    // Keep popup live when image is loaded
    new MutationObserver(() => {
      if (hidden) popup.textContent = infoText();
    }).observe(document.getElementById('image-info'), { childList: true, characterData: true, subtree: true });
  }
}
define('stencil-toolbar', StencilToolbar);
