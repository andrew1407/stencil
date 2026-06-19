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
                    <span class="coord-title" id="coord-title">Last Line Points</span>
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
                </div>
            </div>
    `;
  }
  static template() { return hostTag('stencil-main-content', 'class="main-content"', StencilMainContent.inner()); }

  wire(_app) {
    const btn = document.getElementById('toggle-coord-panel');
    const panel = document.getElementById('coord-panel');
    const title = document.getElementById('coord-title');
    const body = document.getElementById('coord-body');
    let hidden = false;

    btn.addEventListener('click', () => {
      hidden = !hidden;
      panel.classList.toggle('coord-collapsed', hidden);
      btn.innerHTML = hidden ? icon('chevron-right') : icon('chevron-down');
      btn.dataset.title = hidden ? 'Show Last Line Points' : 'Hide panel';
      btn.title = hotkeys.hkTitle(hidden ? 'Show Last Line Points' : 'Hide panel', 'togglePointsList');
    });
  }
}
define('stencil-main-content', StencilMainContent);
