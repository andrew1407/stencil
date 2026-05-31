import { StencilElement, hostTag, define } from './base.js';
// ── Component: selected-line editor panel ───────────────────────
// Markup only; its inputs are wired by DrawingApp via global ids.
export class StencilSelectionPanel extends StencilElement {
  static inner() {
    return `
            <div class="selection-panel-inner">
                <span class="selection-label">✏️ Selected Line:</span>
                <div class="control-group">
                    <label>Color:</label>
                    <input type="color" id="selColor">
                </div>
                <div class="control-group">
                    <label>Thickness:</label>
                    <input type="number" id="selThickness" min="1" max="20" style="width:70px">
                </div>
                <div class="control-group">
                    <label>Marker Size:</label>
                    <input type="number" id="selMarkerSize" min="1" max="30" style="width:70px">
                </div>
                <div class="control-group">
                    <label>Style:</label>
                    <select id="selStyle">
                        <option value="solid">Solid</option>
                        <option value="dashed">Dashed</option>
                        <option value="dotted">Dotted</option>
                    </select>
                </div>
                <div class="control-group" id="selFillGroup" style="display:none;">
                    <label title="Locked area fill"><input type="checkbox" id="selFillEnabled" style="vertical-align:middle;"> Fill:</label>
                    <input type="color" id="selFill" title="Area fill color">
                    <button id="selFillClear" type="button" title="Clear fill (make transparent)" style="background:#e67e22;padding:6px 10px;">✕</button>
                </div>
                <button id="selDeselect" class="deselect-btn">✕ Deselect</button>
            </div>
    `;
  }
  static template() { return hostTag('stencil-selection-panel', 'id="selectionPanel" style="display:none;"', StencilSelectionPanel.inner()); }
}
define('stencil-selection-panel', StencilSelectionPanel);
