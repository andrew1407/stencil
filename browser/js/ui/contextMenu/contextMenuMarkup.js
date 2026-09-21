import { icon } from '../icons.js';
import { ctxArrow } from '../ctx/ctxArrow.js';
import { EXPORT_VARIANTS, EXPORT_VARIANT_LABELS, EXPORT_VARIANT_ICONS } from '../export/exportVariants.js';

// The variant rows of a Copy/Download Image flyout (exportVariants.js owns labels, glyphs
// and order). split/current share the primary combo, filled by syncState.
const exportVariantRows = (prefix, currentIcon, hks) => EXPORT_VARIANTS.map((v) => {
  const hk = v === 'split' || v === 'current'
    ? `<span class="ctx-hotkey" id="${prefix}-${v}-hk"></span>`
    : `<span class="ctx-hotkey" data-hk="${hks[v][0]}">${hks[v][1]}</span>`;
  return `<div class="ctx-item ctx-sub-item" id="${prefix}-${v}"${v === 'split' ? ' style="display:none;"' : ''}>`
    + `<span class="ctx-icon">${icon(v === 'current' ? currentIcon : EXPORT_VARIANT_ICONS[v])}</span>`
    + `<span class="ctx-label">${EXPORT_VARIANT_LABELS[v]}</span>${hk}</div>`;
}).join('\n                        ');

// The menu's static markup; state (checks, hotkey chips, visibility) is written by syncState.
export function contextMenuInner() {
    return `
        <!-- Fit zoom to window — FIRST: the most-reached-for entry, and a plain item, so it
             is a single click with no submenu to traverse. -->
        <div class="ctx-item" id="ctx-fit-window"><span class="ctx-icon">${icon('fit')}</span><span class="ctx-label">Fit to Window</span><span class="ctx-hotkey" data-hk="resetZoom">Alt+0</span>${ctxArrow()}</div>
        <!-- Image / Layout submenu -->
        <div class="ctx-item" id="ctx-layout-menu">
            <span class="ctx-icon">${icon('folder')}</span><span class="ctx-label">Image / Layout</span>${ctxArrow(true)}
            <div class="ctx-sub" id="ctx-layout-sub">
                <div class="ctx-sub-label">Image</div>
                <!-- Copy Image: a nested flyout of the shared variant rows (exportVariants.js;
                     Alt+hover any row previews it — js/ui/exportPreview.js). This opener row
                     carries no hotkey chip: the combo already lives on the primary variant row.
                     No row in this flyout nests further, so its rows reserve no arrow column. -->
                <div class="ctx-item ctx-sub-item" id="ctx-copy-img">
                    <span class="ctx-icon">${icon('copy')}</span><span class="ctx-label">Copy Image</span>${ctxArrow(true)}
                    <div class="ctx-sub ctx-sub-nested" id="ctx-copy-img-sub">
                        ${exportVariantRows('ctx-copy-img', 'copy',
    { original: ['copyImageOriginal', 'Ctrl+Shift+C'], tint: ['copyImageTint', 'Ctrl+Alt+C'] })}
                    </div>
                </div>
                <div class="ctx-item ctx-sub-item" id="ctx-paste-img"><span class="ctx-icon">${icon('paste')}</span><span class="ctx-label">Paste Image</span><span class="ctx-hotkey" data-hk="paste">Ctrl+V</span>${ctxArrow()}</div>
                <!-- Download Image: the same shared variant rows and rules as Copy Image. -->
                <div class="ctx-item ctx-sub-item" id="ctx-dl-img">
                    <span class="ctx-icon">${icon('download')}</span><span class="ctx-label">Download Image</span>${ctxArrow(true)}
                    <div class="ctx-sub ctx-sub-nested" id="ctx-dl-img-sub">
                        ${exportVariantRows('ctx-dl-img', 'download',
    { original: ['saveImageOriginal', 'Ctrl+Alt+D'], tint: ['saveImageTint', 'Ctrl+Shift+Alt+D'] })}
                    </div>
                </div>
                <div class="ctx-item ctx-sub-item" id="ctx-share-img" style="display:none;"><span class="ctx-icon">${icon('share')}</span><span class="ctx-label">Share Image</span>${ctxArrow()}</div>
                <div class="ctx-sep"></div>
                <div class="ctx-sub-label">Layout (JSON)</div>
                <div class="ctx-item ctx-sub-item" id="ctx-copy-layout"><span class="ctx-icon">${icon('copy')}</span><span class="ctx-label">Copy Layout</span><span class="ctx-hotkey" data-hk="copyLayout">Ctrl+Alt+C</span>${ctxArrow()}</div>
                <div class="ctx-item ctx-sub-item" id="ctx-paste-layout"><span class="ctx-icon">${icon('paste')}</span><span class="ctx-label">Paste Layout</span><span class="ctx-hotkey" data-hk="paste">Ctrl+V</span>${ctxArrow()}</div>
                <div class="ctx-item ctx-sub-item" id="ctx-dl-layout"><span class="ctx-icon">${icon('file-text')}</span><span class="ctx-label">Download Layout</span>${ctxArrow()}</div>
                <div class="ctx-item ctx-sub-item" id="ctx-ul-layout"><span class="ctx-icon">${icon('upload')}</span><span class="ctx-label">Upload Layout</span>${ctxArrow()}</div>
            </div>
        </div>
        <!-- Fullscreen toggle -->
        <div class="ctx-item" id="ctx-fullscreen"><span class="ctx-icon">${icon('maximize')}</span><span class="ctx-label" id="ctx-fs-label">Enter Fullscreen</span><span class="ctx-hotkey" data-hk="fullscreen">Alt+F</span>${ctxArrow()}</div>
        <div class="ctx-sep"></div>
        <!-- Stencil Script: a submenu parent whose flyout is a compact editor, hung off this
             row by ctxScript.js. Above it the Assistant entry syncAssistant builds. The
             shortcut still opens the full window. -->
        <div class="ctx-item" id="ctx-script"><span class="ctx-icon">${icon('script')}</span><span class="ctx-label">Stencil Script</span><span class="ctx-hotkey" data-hk="openScript">Alt+Shift+S</span>${ctxArrow(true)}</div>
        <!-- Drawing -->
        <div class="ctx-item" id="ctx-draw-toggle"><span class="ctx-icon">${icon('play', { size: 14 })}</span><span class="ctx-label" id="ctx-draw-label">Start Drawing</span><span class="ctx-hotkey" id="ctx-draw-hotkey" data-hk="startDraw">Alt+A</span>${ctxArrow()}</div>
        <div class="ctx-item" id="ctx-draw-line"><span class="ctx-icon">${icon('line')}</span><span class="ctx-label">Draw Line</span>${ctxArrow()}</div>
        <div class="ctx-item" id="ctx-draw-rect"><span class="ctx-icon">${icon('rect')}</span><span class="ctx-label">Draw Rectangle</span>${ctxArrow()}</div>
        <div class="ctx-sep"></div>
        <!-- Toggles -->
        <div class="ctx-item" id="ctx-show-points"><span class="ctx-check" id="ctx-chk-points">${icon('check', { size: 14 })}</span><span class="ctx-label">Show Points</span><span class="ctx-hotkey" data-hk="togglePoints">Alt+P</span>${ctxArrow()}</div>
        <div class="ctx-item" id="ctx-show-lines"><span class="ctx-check" id="ctx-chk-lines">${icon('check', { size: 14 })}</span><span class="ctx-label">Show Lines</span><span class="ctx-hotkey" data-hk="toggleLines">Alt+L</span>${ctxArrow()}</div>
        <div class="ctx-item" id="ctx-clear-lines"><span class="ctx-icon">${icon('eraser')}</span><span class="ctx-label">Clear All Lines</span><span class="ctx-hotkey" data-hk="clearAllLines">Alt+W</span>${ctxArrow()}</div>
        <div class="ctx-sep"></div>
        <!-- Style submenu -->
        <div class="ctx-item" id="ctx-style-menu">
            <span class="ctx-icon">${icon('palette')}</span><span class="ctx-label">Style</span>${ctxArrow(true)}
            <div class="ctx-sub" id="ctx-style-sub">
                <div class="ctx-row">
                    <label>Point Size</label>
                    <input type="number" class="ctx-num" id="ctx-point-size" min="1" max="30">
                </div>
                <div class="ctx-row">
                    <label>Line Thickness</label>
                    <input type="number" class="ctx-num" id="ctx-thickness" min="1" max="20">
                </div>
                <div class="ctx-sub-label">Line Style</div>
                <div class="ctx-radio-group" id="ctx-style-radios">
                    <label class="ctx-radio-item"><input type="radio" name="ctxLineStyle" value="solid"> Solid</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxLineStyle" value="dashed"> Dashed</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxLineStyle" value="dotted"> Dotted</label>
                </div>
            </div>
        </div>
        <!-- Filter submenu -->
        <div class="ctx-item" id="ctx-filter-menu">
            <span class="ctx-icon">${icon('image')}</span><span class="ctx-label">Image Filter</span><span class="ctx-hotkey" data-hk="cycleFilter">Alt+B</span>${ctxArrow(true)}
            <div class="ctx-sub" id="ctx-filter-sub">
                <div class="ctx-sub-label">Filter</div>
                <div class="ctx-radio-group" id="ctx-filter-radios">
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="none"> None</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="bw"> Black &amp; White</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="sepia"> Sepia</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="invert"> Invert</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="contour"> Contour</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="custom"> Custom Tint</label>
                </div>
                <div id="ctx-tint-row">
                    <label>Tint Color</label>
                    <input type="color" class="ctx-color" id="ctx-tint-color">
                </div>
            </div>
        </div>
        <!-- Transformation submenu -->
        <div class="ctx-item" id="ctx-transform-menu">
            <span class="ctx-icon">${icon('function')}</span><span class="ctx-label">Transformation</span>${ctxArrow(true)}
            <div class="ctx-sub" id="ctx-transform-sub">
                <div class="ctx-sub-label">Coordinate Formulas</div>
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-allow-formulas"> Allow Formulas</label>
                <div id="ctx-formula-inputs" style="display:none;padding:5px 14px 8px;">
                    <div style="margin-top:4px;display:flex;flex-direction:column;gap:5px;">
                        <div style="display:flex;align-items:center;gap:6px;">
                            <label style="font-size:12px;color:var(--text-muted);min-width:36px;font-weight:normal;">x(x)=</label>
                            <input type="text" id="ctx-formula-x" class="ctx-formula-input" placeholder="e.g. x + 9">
                        </div>
                        <div style="display:flex;align-items:center;gap:6px;">
                            <label style="font-size:12px;color:var(--text-muted);min-width:36px;font-weight:normal;">y(y)=</label>
                            <input type="text" id="ctx-formula-y" class="ctx-formula-input" placeholder="e.g. (y-7)*4">
                        </div>
                        <div id="ctx-formula-error" style="font-size:11px;color:var(--danger);display:none;align-items:center;gap:5px;">${icon('alert', { size: 13 })} Invalid formula</div>
                    </div>
                </div>
            </div>
        </div>
        <!-- Tooltip submenu -->
        <div class="ctx-item" id="ctx-tooltip-menu">
            <span class="ctx-icon">${icon('message')}</span><span class="ctx-label">Tooltip</span>${ctxArrow(true)}
            <div class="ctx-sub" id="ctx-tooltip-sub">
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-tt-enabled" checked> Show Tooltips</label>
                <div class="ctx-sep"></div>
                <div class="ctx-sub-label">Show in Tooltip</div>
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-tt-page" checked> Page (cm)</label>
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-tt-screen" checked> Screen (px)</label>
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-tt-coords" checked> To Edge (cm)</label>
            </div>
        </div>
    `;
}
