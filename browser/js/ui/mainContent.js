import { StencilElement, hostTag, define } from './base.js';
import { StencilTooltip } from './tooltip.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
// ── Component: main content (canvas section + coordinates panel) ──
// Owns the canvas/coord-panel markup and the coord-panel collapse behavior.
export class StencilMainContent extends StencilElement {
  static inner() {
    return `
            <div class="canvas-section">
                <div class="canvas-viewport" id="canvas-viewport">
                    <div class="canvas-container" id="canvas-container">
                        <canvas id="canvas"></canvas>
                        <div id="zoom-rect-overlay" style="display:none;position:absolute;border:2px dashed #7c3aed;background:rgba(124,58,237,0.08);pointer-events:none;box-sizing:border-box;"></div>
                        ${StencilTooltip.template()}
                    </div>
                    <div class="idle-create" id="idle-create-wrap">
                        <button id="create-blank-btn" class="idle-create-btn" title="Create a blank image (white, black, or any color) to draw on">
                            <span class="idle-create-icon">${icon('image', { size: 32 })}</span>
                            <span>＋ Blank image</span>
                        </button>
                    </div>
                </div>
                <div class="coord-status" id="coord-status">Open an image to begin</div>
                <div class="drop-hint">${icon('lightbulb', { size: 14 })} Drag &amp; drop an <strong>image</strong> or <strong>.json</strong> anywhere on the page — or paste an image with <strong>Ctrl+V</strong></div>
            </div>

            <div class="coordinates-panel" id="coord-panel">
                <div class="coord-panel-header" id="coord-panel-header">
                    <div class="coord-tabs" role="tablist">
                        <button id="coord-tab-points" class="coord-tab coord-tab-active" role="tab" aria-selected="true" data-tab="points" title="Points of the selected line">Points</button>
                        <button id="coord-tab-lines" class="coord-tab" role="tab" aria-selected="false" data-tab="lines" title="All lines — select, inspect or remove">Lines</button>
                    </div>
                    <button id="toggle-coord-panel" class="btn-icon" data-hk-title="togglePointsList" data-title="Hide panel" title="Hide panel">${icon('chevron-down')}</button>
                </div>
                <div id="coord-body">
                <table class="coordinates-table" id="coordinates-table">
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
                    <tbody id="coordinates-body">
                        <tr>
                            <td colspan="6" class="empty-message">No points yet.</td>
                        </tr>
                    </tbody>
                </table>
                <div id="lines-list" class="lines-list" role="tabpanel" style="display:none;"></div>
                </div>
            </div>
    `;
  }
  static template() { return hostTag('stencil-main-content', 'class="main-content"', StencilMainContent.inner()); }

  wire(app) {
    const btn = document.getElementById('toggle-coord-panel');
    const panel = document.getElementById('coord-panel');
    let hidden = false;

    btn.addEventListener('click', () => {
      hidden = !hidden;
      panel.classList.toggle('coord-collapsed', hidden);
      btn.innerHTML = hidden ? icon('chevron-right') : icon('chevron-down');
      btn.dataset.title = hidden ? 'Show Last Line Points' : 'Hide panel';
      btn.title = hotkeys.hkTitle(hidden ? 'Show Last Line Points' : 'Hide panel', 'togglePointsList');
    });

    // Points | Lines tabs — the Points tab is the existing per-line coordinate table;
    // the Lines tab lists every committed line (select/inspect/remove). Switching to Lines
    // reveals #lines-list and asks the app to (re)build it; back to Points restores the table.
    const tabPoints = document.getElementById('coord-tab-points');
    const tabLines = document.getElementById('coord-tab-lines');
    const table = document.getElementById('coordinates-table');
    const linesList = document.getElementById('lines-list');
    const selectTab = (which) => {
      const onLines = which === 'lines';
      tabLines.classList.toggle('coord-tab-active', onLines);
      tabPoints.classList.toggle('coord-tab-active', !onLines);
      tabLines.setAttribute('aria-selected', onLines ? 'true' : 'false');
      tabPoints.setAttribute('aria-selected', onLines ? 'false' : 'true');
      table.style.display = onLines ? 'none' : '';
      linesList.style.display = onLines ? '' : 'none';
      if (onLines && app) app.renderLinesList();
    };
    tabPoints.addEventListener('click', () => selectTab('points'));
    tabLines.addEventListener('click', () => selectTab('lines'));
  }
}
define('stencil-main-content', StencilMainContent);
