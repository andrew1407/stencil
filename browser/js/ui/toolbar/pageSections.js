// The toolbar's Page and Formula sections: page format + display units, and the f(x,y)
// pill with its two fields. ui/toolbar.js composes them; every input is wired by id.
import { icon } from '../icons.js';
import { pageFormatOptions } from '../../core/settings/units.js';

export const toolbarPageSectionsHtml = () => `            <!-- ── Section: Page ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Page</div>
                <div class="ctrl-section-row">
                    <!-- Custom first, then every named ISO format from PAGE_SIZES with its
                         physical size (re-rendered in the active unit by applyUnitToUI). -->
                    <select id="page-size" data-title="Page size">
                        <option value="custom">Custom</option>
                        ${pageFormatOptions()}
                    </select>
                    <select id="unit-select" data-title="Display units (cm / inches)">
                        <option value="cm">cm</option>
                        <option value="in">in</option>
                    </select>
                    <span id="custom-size-group" style="display:none;align-items:center;gap:6px;">
                        <label style="font-weight:normal;font-size:12px;color:var(--text-muted);">W</label>
                        <input type="number" id="custom-page-width" value="21" min="0.1" max="500" step="0.1" style="width:96px">
                        <label style="font-weight:normal;font-size:12px;color:var(--text-muted);">H</label>
                        <input type="number" id="custom-page-height" value="29.7" min="0.1" max="500" step="0.1" style="width:96px">
                    </span>
                </div>
            </div>

            <!-- ── Section: Formula ──
                 Its OWN section, not a tail of Page (desktop parity: MainWindowToolbar.cpp
                 builds the same named cluster between PAGE and DATA). The two fields are
                 wide, so inside Page every toggle of the pill resized that section and the
                 whole wrapping row re-flowed around it — the sections after it jumped a row
                 (user report, with a picture). On its own the growth is its own. -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Formula</div>
                <div class="ctrl-section-row">
                    <label class="pill-toggle" data-title="Transform page coordinates with a formula f(x,y)">
                        <input type="checkbox" id="allow-formulas"> 𝑓(x,y)
                    </label>
                    <span id="formula-inputs" style="display:none;align-items:center;gap:6px;">
                        <input type="text" id="formula-x" placeholder="x(x)=" style="width:180px;font-family:monospace;font-size:12px;">
                        <input type="text" id="formula-y" placeholder="y(y)=" style="width:180px;font-family:monospace;font-size:12px;">
                        <span id="formula-error" data-title="Invalid formula" style="color:var(--danger);display:none;">${icon('alert', { size: 15 })}</span>
                    </span>
                </div>
            </div>`;
