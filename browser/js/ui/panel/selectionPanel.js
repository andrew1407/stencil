import { StencilElement, hostTag, define } from '../base.js';
import { icon } from '../icons.js';
import { fillState } from '../../core/layout.js';
import { pointColorOf } from '../../core/draw/renderer.js';
import { notify, cssColorParts, cssWithAlpha, writeColorPair, fillFromPair, NO_FILL } from '../../utils.js';
import { surfaceIn, surfaceOut, settleSurface, dockAwayPoint, revealControls } from '../motion.js';
import { syncFsTriggers } from '../fullscreen/panels.js';
import { THICKNESS_RANGE, POINT_SIZE_RANGE } from '../../core/settings/limits.js';
import { applySelectionChange } from '../../core/line/selection.js';
import { renderLinesList } from './lines/list.js';
// ── Component: selected-line editor panel ───────────────────────
// Markup only; ui/bindings/selectionPanel.js wires its inputs by id.
export class StencilSelectionPanel extends StencilElement {
  static inner() {
    return `
            <div class="selection-panel-inner">
                <span class="selection-label">${icon('pencil', { size: 14 })} Selected Line:</span>
                <span class="sel-sep" aria-hidden="true"></span>
                <div class="control-group">
                    <label>Line Color:</label>
                    <input type="color" id="sel-color">
                    <input type="number" id="sel-alpha" class="alpha-input" min="0" max="255" step="1"
                           data-title="Line opacity&#10;0-255, the alpha byte itself: 255 is solid, 0 invisible.">
                </div>
                <div class="control-group">
                    <label>Point Color:</label>
                    <input type="color" id="sel-point-color">
                    <input type="number" id="sel-point-alpha" class="alpha-input" min="0" max="255" step="1"
                           data-title="Point opacity&#10;0-255, the alpha byte itself: 255 is solid, 0 invisible.">
                </div>
                <span class="sel-sep" aria-hidden="true"></span>
                <div class="control-group">
                    <label>Thickness:</label>
                    <input type="number" id="sel-thickness" ${THICKNESS_RANGE} style="width:70px">
                </div>
                <div class="control-group">
                    <label>Point Size:</label>
                    <input type="number" id="sel-point-size" ${POINT_SIZE_RANGE} style="width:70px">
                </div>
                <div class="control-group">
                    <label>Style:</label>
                    <select id="sel-style">
                        <option value="solid">Solid</option>
                        <option value="dashed">Dashed</option>
                        <option value="dotted">Dotted</option>
                    </select>
                </div>
                <span class="sel-sep" id="sel-fill-sep" aria-hidden="true" style="display:none;"></span>
                <div class="control-group" id="sel-fill-group" style="display:none;">
                    <label>Fill:</label>
                    <input type="color" id="sel-fill">
                    <input type="number" id="sel-fill-alpha" class="alpha-input" min="0" max="255" step="1"
                           data-title="Fill opacity&#10;0-255, the alpha byte itself: 255 is solid, 0 invisible.">
                    <button id="sel-fill-clear" type="button" class="btn-icon fill-clear"
                            data-title="Clear fill — make the area transparent again">${icon('rect', { size: 13 })}</button>
                    <button id="sel-unchain" type="button" class="btn-icon-text"
                            data-title="Unchain area&#10;Break the closed shape back into an open line.&#10;Alt+Ctrl+drag on the line does the same, at the spot you pull."
                            style="padding:6px 10px;">${icon('link', { size: 13 })}<span>Unchain</span></button>
                </div>
                <span class="sel-sep" aria-hidden="true"></span>
                <button id="sel-deselect" class="deselect-btn btn-icon-text">${icon('x', { size: 13 })}<span>Deselect</span></button>
            </div>
    `;
  }
  static template() { return hostTag('stencil-selection-panel', 'id="selection-panel" style="display:none;"', StencilSelectionPanel.inner()); }
}
define('stencil-selection-panel', StencilSelectionPanel);

// Anchors to #image-info rather than the bar's own (dis)appearing rect. `closing`:
// #image-info's rect is still pre-close, so predict it (bar's top + #image-info's height).
export const barDustPoint = (el, closing = false) => {
  const info = document.getElementById('image-info');
  const r = info?.getBoundingClientRect?.();
  if (r && r.width > 0 && r.height > 0) {
    const y = closing ? el.getBoundingClientRect().top + r.height : r.bottom;
    return { x: r.left + r.width / 2, y };
  }
  // No image-info to anchor to (shouldn't happen while a line is selected) — fall back
  // to the bar's own geometry.
  return dockAwayPoint(el.getBoundingClientRect(), 'bottom');
};

// Populate + show the panel (and its fullscreen mirror) for the selected `line`.
export function showSelectionPanel(app, line) {
  // The swatch takes the 7-char hex and the slider the alpha: <input type="color"> cannot
  // carry the alpha byte, so a stored `#rrggbbaa` is split across the pair (utils.js).
  writeColorPair('sel-color', 'sel-alpha', line.color);
  // A line with no point colour of its own shows the colour it actually draws in (its
  // stroke) rather than a stale/empty swatch — matching core's pointColorOr fallback.
  writeColorPair('sel-point-color', 'sel-point-alpha', pointColorOf(line));
  document.getElementById('sel-thickness').value = line.thickness;
  document.getElementById('sel-point-size').value = line.pointSize ?? app.pointSize;
  // A line with no style of its own draws solid (desktop SelectedLineBar does the same).
  document.getElementById('sel-style').value = line.style || 'solid';
  // Fill control appears only for locked areas
  const fillGroup = document.getElementById('sel-fill-group');
  if (fillGroup) {
    const fillSep = document.getElementById('sel-fill-sep');
    // The group SLIDES open and closes, dusting as it goes (revealControls) — it used to
    // pop in and out, which reads as the bar jumping. Its separator travels with it.
    if (line.locked) {
      revealControls(fillGroup, true, 'flex');
      if (fillSep) revealControls(fillSep, true, 'block');
      const fs = fillState(line, app.defaultFillColor);
      // No on/off tick: a fill IS its rgba, and 0 alpha is what "none" means. An
      // unfilled area shows the default colour at 0 so picking one is a single move.
      writeColorPair('sel-fill', 'sel-fill-alpha',
                     fs.enabled ? line.fillColor : cssWithAlpha(fs.value, 0));
    } else {
      revealControls(fillGroup, false);
      // …and so does its separator, or the one before it and the one after the group
      // end up side by side with nothing between them.
      if (fillSep) revealControls(fillSep, false);
    }
  }
  const panel = document.getElementById('selection-panel');
  // Only on the hidden -> visible edge: re-populating an already-open bar (switching the
  // selected line) must not replay the gather.
  const wasHidden = panel.style.display !== 'block';
  panel.style.display = 'block';
  if (wasHidden && !surfaceIn(panel, barDustPoint(panel), { belowChat: true })) settleSurface(panel);
  syncFsSelectionPanel(app, line);
  renderLinesList(app);
}

// Hide the selection panel and its fullscreen mirror.
export function hideSelectionPanels() {
  const selPanel = document.getElementById('selection-panel');
  if (selPanel) {
    if (selPanel.style.display === 'block') {
      if (!surfaceOut(selPanel, barDustPoint(selPanel, /* closing */ true), { belowChat: true })) settleSurface(selPanel);
    } else settleSurface(selPanel);
    selPanel.style.display = 'none';
  }
  const fsPanel = document.getElementById('fs-selection-panel');
  if (fsPanel) fsPanel.style.display = 'none';
  syncFsTriggers();
}

// Apply the locked-area fill from the selection panel controls; `commit:false` only previews.
export function applyFill(app, { commit = true } = {}) {
  applySelectionChange(app, 'fillColor', fillFromPair('sel-fill', 'sel-fill-alpha'), { commit });
}

// Rebuild + wire the fullscreen mirror of the panel for `line` (hidden outside fullscreen).
export function syncFsSelectionPanel(app, line) {
  const fsPanel = document.getElementById('fs-selection-panel');
  if (!fsPanel) return;
  const isFS = document.body.classList.contains('fullscreen-mode');
  if (!isFS || !line) { fsPanel.style.display = 'none'; return; }
  // Always start at top:0; updateFsSelectionTop (called from show/hideControlsPanel) handles offset
  const fsCtrls = document.getElementById('fs-controls-panel');
  const ctrlsVisible = fsCtrls && fsCtrls.classList.contains('fs-panel-visible');
  fsPanel.style.transition = 'none'; // no transition on initial placement
  fsPanel.style.top = ctrlsVisible ? fsCtrls.getBoundingClientRect().height + 'px' : '0px';
  fsPanel.style.display = 'block';
  // Re-enable transition after placement
  requestAnimationFrame(() => { fsPanel.style.transition = ''; });
  requestAnimationFrame(syncFsTriggers);   // the top band spans the overlay too
  const fs = fillState(line, app.defaultFillColor);
  fsPanel.innerHTML = `<div class="selection-panel-inner">
            <span class="selection-label">${icon('pencil', { size: 14 })} Selected Line:</span>
            <div class="control-group"><label>Line Color:</label>
                <input type="color" id="fs-sel-color" value="${line.color}" style="width:60px;height:34px;cursor:pointer;border:1px solid var(--border-main);border-radius:4px;"></div>
            <div class="control-group"><label>Point Color:</label>
                <input type="color" id="fs-sel-point-color" value="${pointColorOf(line)}" style="width:60px;height:34px;cursor:pointer;border:1px solid var(--border-main);border-radius:4px;"></div>
            <div class="control-group"><label>Thickness:</label>
                <input type="number" id="fs-sel-thickness" value="${line.thickness}" ${THICKNESS_RANGE} style="width:70px;background:var(--input-bg);color:var(--input-text);border:1px solid var(--border-main);border-radius:4px;padding:6px 8px;font-size:14px;"></div>
            <div class="control-group"><label>Point Size:</label>
                <input type="number" id="fs-sel-point-size" value="${line.pointSize ?? app.pointSize}" ${POINT_SIZE_RANGE} style="width:70px;background:var(--input-bg);color:var(--input-text);border:1px solid var(--border-main);border-radius:4px;padding:6px 8px;font-size:14px;"></div>
            <div class="control-group"><label>Style:</label>
                <select id="fs-sel-style" style="background:var(--input-bg);color:var(--input-text);border:1px solid var(--border-main);border-radius:4px;padding:6px 8px;font-size:14px;">
                    <option value="solid"${line.style==='solid'?' selected':''}>Solid</option>
                    <option value="dashed"${line.style==='dashed'?' selected':''}>Dashed</option>
                    <option value="dotted"${line.style==='dotted'?' selected':''}>Dotted</option>
                </select></div>
            ${line.locked ? `<div class="control-group"><label>Fill:</label>
                <input type="color" id="fs-sel-fill" value="${fs.value}" style="width:46px;height:26px;cursor:pointer;border:1px solid var(--border-main);border-radius:4px;padding:4px 6px;">
                <button id="fs-sel-fill-clear" type="button" style="background:#e67e22;color:#fff;border:none;padding:6px 10px;border-radius:4px;cursor:pointer;font-size:13px;">${icon('x', { size: 13 })}</button></div>` : ''}
            <button id="fs-sel-deselect" class="btn-icon-text" style="background:#e67e22;color:#fff;border:none;padding:6px 12px;border-radius:4px;cursor:pointer;font-size:13px;">${icon('x', { size: 13 })}<span>Deselect</span></button>
        </div>`;
  const wireFsColor = (id, mainId, prop) => ['input', 'change'].forEach((type) =>
    fsPanel.querySelector(id).addEventListener(type, (e) => {
      applySelectionChange(app, prop, e.target.value, { commit: type === 'change' });
      document.getElementById(mainId).value = e.target.value;
    }));
  wireFsColor('#fs-sel-color', 'sel-color', 'color');
  wireFsColor('#fs-sel-point-color', 'sel-point-color', 'pointColor');
  fsPanel.querySelector('#fs-sel-thickness').addEventListener('change', e => {
    applySelectionChange(app, 'thickness', parseInt(e.target.value, 10));
    document.getElementById('sel-thickness').value = e.target.value;
  });
  fsPanel.querySelector('#fs-sel-point-size').addEventListener('change', e => {
    applySelectionChange(app, 'pointSize', parseInt(e.target.value, 10));
    document.getElementById('sel-point-size').value = e.target.value;
  });
  fsPanel.querySelector('#fs-sel-style').addEventListener('change', e => {
    applySelectionChange(app, 'style', e.target.value);
    document.getElementById('sel-style').value = e.target.value;
  });
  const fsFill = fsPanel.querySelector('#fs-sel-fill');
  if (fsFill) {
    const applyFsFill = (color, commit = true) => {
      if (!applySelectionChange(app, 'fillColor', color, { commit })) return;
      const mainFill = document.getElementById('sel-fill');
      if (mainFill && color !== NO_FILL) mainFill.value = cssColorParts(color).hex;
    };
    fsFill.addEventListener('input', () => applyFsFill(fsFill.value, false));
    fsFill.addEventListener('change', () => applyFsFill(fsFill.value));
    const fsFillClear = fsPanel.querySelector('#fs-sel-fill-clear');
    if (fsFillClear) fsFillClear.addEventListener('click', () => {
      applyFsFill(NO_FILL);
      notify('Fill cleared (transparent)', 'ok');
    });
  }
  fsPanel.querySelector('#fs-sel-deselect').addEventListener('click', () => app.deselectLine());
}
