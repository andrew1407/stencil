import { StencilElement, hostTag, define } from './base.js';
import { DRAW_MODE_ICON } from '../core/drawingApp.js';
import { hotkeys } from '../core/hotkeys.js';
// ── Component: toolbar (controls-wrapper + all 8 sections) ──────
// Owns the controls markup and the collapse/hints behavior. The individual
// inputs/buttons are wired by DrawingApp via global ids.
export class StencilToolbar extends StencilElement {
  static inner() {
    return `
            <div class="controls-topbar">
                <button id="toggle-controls" data-hk-title="toggleControls" title="Hide controls (Alt+C)">▲ Controls</button>
                <span class="project-name-field" style="flex:0 1 240px;min-width:90px;display:inline-flex;align-items:center;gap:4px;">
                    <input id="project-name-input" type="text" placeholder="No project" title="Project name — double-click to rename" readonly disabled
                        style="flex:1 1 auto;min-width:0;font-size:13px;font-weight:600;color:var(--text);background:transparent;border:1px solid transparent;border-radius:6px;padding:3px 8px;">
                    <button id="project-name-edit" class="name-edit-btn name-edit-pencil" type="button" title="Rename project" style="display:none;">✎</button>
                    <button id="project-name-accept" class="name-edit-btn name-edit-accept" type="button" title="Save name" style="display:none;">✓</button>
                    <button id="project-name-cancel" class="name-edit-btn name-edit-cancel" type="button" title="Cancel" style="display:none;">✗</button>
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
                    <select id="image-filter" data-hk-title="cycleFilter" title="Image Filter (Alt+B)">
                        <option value="none">No Filter</option>
                        <option value="bw">B&amp;W</option>
                        <option value="sepia">Sepia</option>
                        <option value="custom">Tint</option>
                    </select>
                    <input type="color" id="filter-color" value="#7c3aed" title="Tint color" style="display:none;width:36px;height:30px;padding:2px;cursor:pointer;border-radius:4px;">
                    <button id="crop-image" title="Crop image — pick the page-shaped region to show on the canvas">✂ Crop</button>
                    <button id="rotate-left" data-hk-title="rotateImageLeft" title="Rotate image left (Alt+R)">↺</button>
                    <button id="rotate-right" data-hk-title="rotateImageRight" title="Rotate image right (Alt+Shift+R)">↻</button>
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
                    <button id="start-drawing" data-hk-title="startDraw" title="Start Drawing (Alt+A)">▶ Start</button>
                    <button id="stop-drawing" disabled data-hk-title="stopDraw" title="Stop Drawing (Alt+S)">■ Stop</button>
                    <button id="draw-mode-toggle" title="Drawing mode: Line (click to switch to Rectangle)">${DRAW_MODE_ICON.line} Line</button>
                    <button id="undo" disabled data-hk-title="undo" title="Undo (Ctrl+Z)">↩</button>
                    <button id="redo" disabled data-hk-title="redo" title="Redo (Ctrl+Shift+Z)">↪</button>
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
                    <button id="clear-all-lines" class="danger" data-hk-title="clearAllLines" title="Clear All Lines (Alt+W)">🗑 Clear</button>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Zoom ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Zoom</div>
                <div class="ctrl-section-row">
                    <div class="zoom-controls">
                        <button id="zoom-out" title="Zoom out">−</button>
                        <input type="number" id="zoom-input" value="100" min="5" max="500" title="Zoom % (Enter to apply)">
                        <span style="font-size:13px;font-weight:bold;color:var(--text-muted)">%</span>
                        <button id="zoom-in" title="Zoom in">+</button>
                        <button id="zoom-fit" data-hk-title="resetZoom" title="Fit to window (Alt+0)">⊡</button>
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
                        <span id="formula-error" style="font-size:11px;color:#dc3545;display:none;">⚠</span>
                    </span>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Data ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Data</div>
                <div class="ctrl-section-row">
                    <button id="download-json" title="Download Layout JSON">⬇ JSON</button>
                    <button id="copy-json-btn" data-hk-title="copyLayout" title="Copy Layout JSON (Alt+J)">📋</button>
                    <button id="save-image" title="Save Image">💾</button>
                    <input type="file" id="upload-json" accept=".json" style="display:none;">
                    <button id="upload-json-btn" title="Upload Layout JSON">📂</button>
                    <button id="clear-storage" class="danger" title="Clear saved storage">🗑</button>
                    <span id="save-status" style="font-size:12px;color:#555;min-width:70px;"></span>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: App ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">App</div>
                <div class="ctrl-section-row">
                    <button id="theme-toggle" title="Toggle dark/light theme">🌙</button>
                    <button id="fullscreen-toggle" data-hk-title="fullscreen" title="Fullscreen (Alt+F)">⛶</button>
                    <button id="projects-btn" title="Projects">🗂</button>
                    <button id="links-btn" title="Source &amp; resource links · add image by URL">🔗</button>
                    <button id="incognito-toggle" title="Incognito — edit without saving (choose before adding an image)">🕶</button>
                    <button id="settings-btn" title="Keyboard shortcuts">⚙️</button>
                    <button id="visuals-btn" title="Default visuals & highlight styles">🎨</button>
                    <button id="info-btn" title="Controls & shortcuts help">📋</button>
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
      btn.textContent = hidden ? '▼ Controls' : '▲ Controls';
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
