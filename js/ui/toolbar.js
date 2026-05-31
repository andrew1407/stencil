import { StencilElement, hostTag, define } from './base.js';
// ── Component: toolbar (controls-wrapper + all 8 sections) ──────
// Owns the controls markup and the collapse/hints behavior. The individual
// inputs/buttons are wired by DrawingApp via global ids.
export class StencilToolbar extends StencilElement {
  static inner() {
    return `
            <div class="controls-topbar">
                <button id="toggleControls" title="Hide controls (Alt+C)">▲ Controls</button>
                <span id="hintsBtn" style="display:none;position:relative;cursor:default;font-size:12px;color:var(--text-muted);border:1px solid var(--border-main);border-radius:12px;padding:2px 8px;user-select:none;">
                    ?
                    <span class="hints-popup" id="hintsPopup"></span>
                </span>
            </div>
            <div id="controlsBody">
        <div class="controls">

            <!-- ── Section: Image ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Image</div>
                <div class="ctrl-section-row">
                    <input type="file" id="imageUpload" accept="image/*">
                    <span id="imageSizeDisplay" style="display:none;font-size:12px;color:var(--text-muted);background:var(--bg-info);padding:3px 8px;border-radius:4px;border:1px solid var(--border-main);white-space:nowrap;"></span>
                    <select id="imageFilter" title="Image Filter (Alt+B)">
                        <option value="none">No Filter</option>
                        <option value="bw">B&amp;W</option>
                        <option value="sepia">Sepia</option>
                        <option value="custom">Tint</option>
                    </select>
                    <input type="color" id="filterColor" value="#7c3aed" title="Tint color" style="display:none;width:36px;height:30px;padding:2px;cursor:pointer;border-radius:4px;">
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Drawing style ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Line Style</div>
                <div class="ctrl-section-row">
                    <input type="color" id="lineColor" value="#FFFF00" title="Line color">
                    <input type="number" id="lineThickness" value="2" min="1" max="20" title="Line thickness" style="width:54px">
                    <input type="number" id="markerSize" value="4" min="1" max="30" title="Marker size" style="width:54px">
                    <select id="lineStyle" title="Line style">
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
                    <button id="startDrawing" title="Start Drawing (Alt+A)">▶ Start</button>
                    <button id="stopDrawing" disabled title="Stop Drawing (Alt+S)">■ Stop</button>
                    <button id="drawModeToggle" title="Drawing mode: Line (click to switch to Rectangle)">╱ Line</button>
                    <button id="undo" disabled title="Undo (Ctrl+Z)">↩</button>
                    <button id="redo" disabled title="Redo (Ctrl+Shift+Z)">↪</button>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: View ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">View</div>
                <div class="ctrl-section-row">
                    <label style="font-weight:normal;font-size:13px;cursor:pointer;display:flex;align-items:center;gap:4px;" title="Show Points (Alt+P)">
                        <input type="checkbox" id="showPoints" checked> Points
                    </label>
                    <label style="font-weight:normal;font-size:13px;cursor:pointer;display:flex;align-items:center;gap:4px;" title="Show Lines (Alt+L)">
                        <input type="checkbox" id="showLines" checked> Lines
                    </label>
                    <button id="clearAllLines" class="danger" title="Clear All Lines (Alt+W)">🗑 Clear</button>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Zoom ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Zoom</div>
                <div class="ctrl-section-row">
                    <div class="zoom-controls">
                        <button id="zoomOut" title="Zoom out">−</button>
                        <input type="number" id="zoomInput" value="100" min="5" max="500" title="Zoom % (Enter to apply)">
                        <span style="font-size:13px;font-weight:bold;color:var(--text-muted)">%</span>
                        <button id="zoomIn" title="Zoom in">+</button>
                        <button id="zoomFit" title="Fit to window (Alt+0)">⊡</button>
                    </div>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Page ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Page</div>
                <div class="ctrl-section-row">
                    <select id="pageSize" title="Page size">
                        <option value="A3">A3</option>
                        <option value="A4">A4</option>
                        <option value="custom">Custom…</option>
                    </select>
                    <span id="customSizeGroup" style="display:none;align-items:center;gap:6px;">
                        <label style="font-weight:normal;font-size:12px;color:var(--text-muted);">W</label>
                        <input type="number" id="customPageWidth" value="21" min="1" max="500" step="0.1" style="width:64px">
                        <label style="font-weight:normal;font-size:12px;color:var(--text-muted);">H</label>
                        <input type="number" id="customPageHeight" value="29.7" min="1" max="500" step="0.1" style="width:64px">
                        <span style="font-size:12px;color:var(--text-muted);">cm</span>
                    </span>
                    <label style="font-weight:normal;font-size:13px;cursor:pointer;display:flex;align-items:center;gap:5px;margin-left:6px;">
                        <input type="checkbox" id="allowFormulas"> 𝑓(x,y)
                    </label>
                    <span id="formulaInputs" style="display:none;align-items:center;gap:6px;">
                        <input type="text" id="formulaX" placeholder="x(x)=" style="width:90px;font-family:monospace;font-size:12px;">
                        <input type="text" id="formulaY" placeholder="y(y)=" style="width:90px;font-family:monospace;font-size:12px;">
                        <span id="formulaError" style="font-size:11px;color:#dc3545;display:none;">⚠</span>
                    </span>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Data ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Data</div>
                <div class="ctrl-section-row">
                    <button id="downloadJSON" title="Download Layout JSON">⬇ JSON</button>
                    <button id="copyJSONBtn" title="Copy Layout JSON (Alt+J)">📋</button>
                    <button id="saveImage" title="Save Image">💾</button>
                    <input type="file" id="uploadJSON" accept=".json" style="display:none;">
                    <button id="uploadJSONBtn" title="Upload Layout JSON">📂</button>
                    <button id="clearStorage" class="danger" title="Clear saved storage">🗑</button>
                    <span id="saveStatus" style="font-size:12px;color:#555;min-width:70px;"></span>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: App ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">App</div>
                <div class="ctrl-section-row">
                    <button id="themeToggle" title="Toggle dark/light theme">🌙</button>
                    <button id="fullscreenToggle" title="Fullscreen (Alt+F)">⛶</button>
                    <button id="projectsBtn" title="Projects">🗂</button>
                    <button id="incognitoToggle" title="Incognito — edit without saving (choose before adding an image)">🕶</button>
                    <button id="settingsBtn" title="Keyboard shortcuts">⚙️</button>
                    <button id="visualsBtn" title="Default visuals & highlight styles">🎨</button>
                    <button id="infoBtn" title="Controls & shortcuts help">📋</button>
                </div>
            </div>

        </div>
            </div><!-- /controlsBody -->
    `;
  }
  static template() { return hostTag('stencil-toolbar', 'class="controls-wrapper"', StencilToolbar.inner()); }

  wire(_app) {
    const btn = document.getElementById('toggleControls');
    const body = document.getElementById('controlsBody');
    const hintsBtn = document.getElementById('hintsBtn');
    const popup = document.getElementById('hintsPopup');
    let hidden = false;

    const infoText = () => {
      const el = document.getElementById('imageInfo');
      return el ? el.textContent : 'Image Size: — | Zoom: Ctrl+Scroll or Alt+Scroll · +/− · ⊡ Fit | Pan: Alt+Drag';
    };

    btn.addEventListener('click', () => {
      hidden = !hidden;
      body.classList.toggle('hidden', hidden);
      btn.textContent = hidden ? '▼ Controls' : '▲ Controls';
      btn.title = hidden ? 'Show controls (Alt+C)' : 'Hide controls (Alt+C)';
      hintsBtn.style.display = hidden ? 'inline-block' : 'none';
      if (hidden) popup.textContent = infoText();
    });

    // Keep popup live when image is loaded
    new MutationObserver(() => {
      if (hidden) popup.textContent = infoText();
    }).observe(document.getElementById('imageInfo'), { childList: true, characterData: true, subtree: true });
  }
}
define('stencil-toolbar', StencilToolbar);
