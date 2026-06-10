import { StencilElement, hostTag, define } from './base.js';
import { StencilTooltip } from './tooltip.js';
// ── Component: main content (canvas section + coordinates panel) ──
// Owns the canvas/coord-panel markup and the coord-panel collapse behavior.
export class StencilMainContent extends StencilElement {
  static inner() {
    return `
            <div class="canvas-section">
                <div class="canvas-viewport" id="canvasViewport">
                    <div class="canvas-container" id="canvasContainer">
                        <canvas id="canvas"></canvas>
                        <div id="zoomRectOverlay" style="display:none;position:absolute;border:2px dashed #007bff;background:rgba(0,123,255,0.08);pointer-events:none;box-sizing:border-box;"></div>
                        ${StencilTooltip.template()}
                    </div>
                </div>
                <div class="coord-status" id="coordStatus">Open an image to begin</div>
                <div class="drop-hint">💡 Drag &amp; drop an <strong>image</strong> or <strong>.json</strong> anywhere on the page — or paste an image with <strong>Ctrl+V</strong></div>
            </div>

            <div class="coordinates-panel" id="coordPanel">
                <div class="coord-panel-header" id="coordPanelHeader">
                    <span class="coord-title" id="coordTitle">Last Line Points</span>
                    <button id="toggleCoordPanel" title="Hide panel (Alt+X)">▼</button>
                </div>
                <div id="coordBody">
                <table class="coordinates-table" id="coordinatesTable">
                    <thead>
                        <tr>
                            <th>#</th>
                            <th>X px</th>
                            <th>Y px</th>
                            <th>X cm</th>
                            <th>Y cm</th>
                            <th style="width:28px;padding:4px;"></th>
                        </tr>
                    </thead>
                    <tbody id="coordinatesBody">
                        <tr>
                            <td colspan="6" class="empty-message">No points yet.</td>
                        </tr>
                    </tbody>
                </table>
                </div>
            </div>
    `;
  }
  static template() { return hostTag('stencil-main-content', 'class="main-content"', StencilMainContent.inner()); }

  wire(_app) {
    const btn = document.getElementById('toggleCoordPanel');
    const panel = document.getElementById('coordPanel');
    const title = document.getElementById('coordTitle');
    const body = document.getElementById('coordBody');
    let hidden = false;

    btn.addEventListener('click', () => {
      hidden = !hidden;
      panel.classList.toggle('coord-collapsed', hidden);
      btn.textContent = hidden ? '▶' : '▼';
      btn.title = hidden ? 'Show Last Line Points (Alt+X)' : 'Hide panel (Alt+X)';
    });
  }
}
define('stencil-main-content', StencilMainContent);
