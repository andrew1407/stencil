import { StencilElement, hostTag, define } from './base.js';
import { icon } from './icons.js';
import { fillState } from '../core/layout.js';
import { pointColorOf } from '../core/renderer.js';
import { notify } from '../utils.js';
import { surfaceIn, surfaceOut, settleSurface, dockAwayPoint } from './motion.js';
// ── Component: selected-line editor panel ───────────────────────
// Markup only; its inputs are wired by DrawingApp via global ids.
export class StencilSelectionPanel extends StencilElement {
  static inner() {
    return `
            <div class="selection-panel-inner">
                <span class="selection-label">${icon('pencil', { size: 14 })} Selected Line:</span>
                <div class="control-group">
                    <label>Line Color:</label>
                    <input type="color" id="sel-color">
                </div>
                <div class="control-group">
                    <label>Point Color:</label>
                    <input type="color" id="sel-point-color">
                </div>
                <div class="control-group">
                    <label>Thickness:</label>
                    <input type="number" id="sel-thickness" min="1" max="20" style="width:70px">
                </div>
                <div class="control-group">
                    <label>Point Size:</label>
                    <input type="number" id="sel-point-size" min="1" max="30" style="width:70px">
                </div>
                <div class="control-group">
                    <label>Style:</label>
                    <select id="sel-style">
                        <option value="solid">Solid</option>
                        <option value="dashed">Dashed</option>
                        <option value="dotted">Dotted</option>
                    </select>
                </div>
                <div class="control-group" id="sel-fill-group" style="display:none;">
                    <label><input type="checkbox" id="sel-fill-enabled" style="vertical-align:middle;"> Fill:</label>
                    <input type="color" id="sel-fill">
                    <button id="sel-fill-clear" type="button" style="background:#e67e22;padding:6px 10px;">${icon('x', { size: 13 })}</button>
                </div>
                <button id="sel-deselect" class="deselect-btn btn-icon-text">${icon('x', { size: 13 })}<span>Deselect</span></button>
            </div>
    `;
  }
  static template() { return hostTag('stencil-selection-panel', 'id="selection-panel" style="display:none;"', StencilSelectionPanel.inner()); }
}
define('stencil-selection-panel', StencilSelectionPanel);

// ── Panel ↔ app sync (extracted from drawingApp.js; DrawingApp keeps thin delegators) ──

// Anchors to #image-info rather than the bar's own (dis)appearing rect — no single
// control opens the bar, so there's no natural origin element otherwise.
// `closing`: #image-info's rect is still pre-close here; predict its post-close position
// (bar's own top + #image-info's height) instead of trusting that stale bottom.
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
  document.getElementById('sel-color').value = line.color;
  // A line with no point colour of its own shows the colour it actually draws in (its
  // stroke) rather than a stale/empty swatch — matching core's pointColorOr fallback.
  document.getElementById('sel-point-color').value = pointColorOf(line);
  document.getElementById('sel-thickness').value = line.thickness;
  document.getElementById('sel-point-size').value = line.pointSize ?? app.pointSize;
  document.getElementById('sel-style').value = line.style;
  // Fill control appears only for locked areas
  const fillGroup = document.getElementById('sel-fill-group');
  if (fillGroup) {
    if (line.locked) {
      fillGroup.style.display = 'flex';
      const fs = fillState(line, app.defaultFillColor);
      document.getElementById('sel-fill-enabled').checked = fs.enabled;
      document.getElementById('sel-fill').value = fs.value;
    } else {
      fillGroup.style.display = 'none';
    }
  }
  const panel = document.getElementById('selection-panel');
  // Only on the hidden -> visible edge: re-populating an already-open bar (switching the
  // selected line) must not replay the gather.
  const wasHidden = panel.style.display !== 'block';
  panel.style.display = 'block';
  if (wasHidden && !surfaceIn(panel, barDustPoint(panel))) settleSurface(panel);
  app.syncFsSelectionPanel(line);
  app.renderLinesList();
}

// Hide the selection panel and its fullscreen mirror.
export function hideSelectionPanels() {
  const selPanel = document.getElementById('selection-panel');
  if (selPanel) {
    if (selPanel.style.display === 'block') {
      if (!surfaceOut(selPanel, barDustPoint(selPanel, /* closing */ true))) settleSurface(selPanel);
    } else settleSurface(selPanel);
    selPanel.style.display = 'none';
  }
  const fsPanel = document.getElementById('fs-selection-panel');
  if (fsPanel) fsPanel.style.display = 'none';
}

// Apply the locked-area fill from the selection panel controls.
export function applyFill(app) {
  if (app.compareReadOnly()) return; // read-only compare view
  if (app.selectedLineIdx === -1) return;
  const line = app.lines[app.selectedLineIdx];
  if (!line) return;
  const enabled = document.getElementById('sel-fill-enabled').checked;
  const color = document.getElementById('sel-fill').value;
  line.fillColor = enabled ? color : 'transparent';
  app.saveHistory();
  app.renderer.redraw();
  app.storage.save();
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
  // Expand top trigger to cover the selection panel
  requestAnimationFrame(() => {
    const trigger = document.getElementById('fs-top-trigger');
    if (trigger) trigger.style.height = Math.max(8, fsPanel.getBoundingClientRect().bottom) + 'px';
  });
  const fs = fillState(line, app.defaultFillColor);
  fsPanel.innerHTML = `<div class="selection-panel-inner">
            <span class="selection-label">${icon('pencil', { size: 14 })} Selected Line:</span>
            <div class="control-group"><label>Line Color:</label>
                <input type="color" id="fs-sel-color" value="${line.color}" style="width:60px;height:34px;cursor:pointer;border:1px solid var(--border-main);border-radius:4px;"></div>
            <div class="control-group"><label>Point Color:</label>
                <input type="color" id="fs-sel-point-color" value="${pointColorOf(line)}" style="width:60px;height:34px;cursor:pointer;border:1px solid var(--border-main);border-radius:4px;"></div>
            <div class="control-group"><label>Thickness:</label>
                <input type="number" id="fs-sel-thickness" value="${line.thickness}" min="1" max="20" style="width:70px;background:var(--input-bg);color:var(--input-text);border:1px solid var(--border-main);border-radius:4px;padding:6px 8px;font-size:14px;"></div>
            <div class="control-group"><label>Point Size:</label>
                <input type="number" id="fs-sel-point-size" value="${line.pointSize ?? app.pointSize}" min="1" max="30" style="width:70px;background:var(--input-bg);color:var(--input-text);border:1px solid var(--border-main);border-radius:4px;padding:6px 8px;font-size:14px;"></div>
            <div class="control-group"><label>Style:</label>
                <select id="fs-sel-style" style="background:var(--input-bg);color:var(--input-text);border:1px solid var(--border-main);border-radius:4px;padding:6px 8px;font-size:14px;">
                    <option value="solid"${line.style==='solid'?' selected':''}>Solid</option>
                    <option value="dashed"${line.style==='dashed'?' selected':''}>Dashed</option>
                    <option value="dotted"${line.style==='dotted'?' selected':''}>Dotted</option>
                </select></div>
            ${line.locked ? `<div class="control-group"><label>Fill:</label>
                <input type="checkbox" id="fs-sel-fill-enabled"${fs.enabled?' checked':''} style="vertical-align:middle;">
                <input type="color" id="fs-sel-fill" value="${fs.value}" style="width:60px;height:34px;cursor:pointer;border:1px solid var(--border-main);border-radius:4px;">
                <button id="fs-sel-fill-clear" type="button" style="background:#e67e22;color:#fff;border:none;padding:6px 10px;border-radius:4px;cursor:pointer;font-size:13px;">${icon('x', { size: 13 })}</button></div>` : ''}
            <button id="fs-sel-deselect" class="btn-icon-text" style="background:#e67e22;color:#fff;border:none;padding:6px 12px;border-radius:4px;cursor:pointer;font-size:13px;">${icon('x', { size: 13 })}<span>Deselect</span></button>
        </div>`;
  fsPanel.querySelector('#fs-sel-color').addEventListener('input', e => {
    app.applySelectionChange('color', e.target.value);
    document.getElementById('sel-color').value = e.target.value;
  });
  fsPanel.querySelector('#fs-sel-point-color').addEventListener('input', e => {
    app.applySelectionChange('pointColor', e.target.value);
    document.getElementById('sel-point-color').value = e.target.value;
  });
  fsPanel.querySelector('#fs-sel-thickness').addEventListener('change', e => {
    app.applySelectionChange('thickness', parseInt(e.target.value));
    document.getElementById('sel-thickness').value = e.target.value;
  });
  fsPanel.querySelector('#fs-sel-point-size').addEventListener('change', e => {
    app.applySelectionChange('point-size', parseInt(e.target.value));
    document.getElementById('sel-point-size').value = e.target.value;
  });
  fsPanel.querySelector('#fs-sel-style').addEventListener('change', e => {
    app.applySelectionChange('style', e.target.value);
    document.getElementById('sel-style').value = e.target.value;
  });
  const fsFillEnabled = fsPanel.querySelector('#fs-sel-fill-enabled');
  const fsFill = fsPanel.querySelector('#fs-sel-fill');
  if (fsFillEnabled && fsFill) {
    const applyFsFill = () => {
      if (app.selectedLineIdx === -1) return;
      const ln = app.lines[app.selectedLineIdx];
      if (!ln) return;
      ln.fillColor = fsFillEnabled.checked ? fsFill.value : 'transparent';
      const mainEnabled = document.getElementById('sel-fill-enabled');
      const mainFill = document.getElementById('sel-fill');
      if (mainEnabled) mainEnabled.checked = fsFillEnabled.checked;
      if (mainFill) mainFill.value = fsFill.value;
      app.saveHistory(); app.renderer.redraw(); app.storage.save();
    };
    fsFillEnabled.addEventListener('change', applyFsFill);
    fsFill.addEventListener('input', () => { fsFillEnabled.checked = true; applyFsFill(); });
    const fsFillClear = fsPanel.querySelector('#fs-sel-fill-clear');
    if (fsFillClear) fsFillClear.addEventListener('click', () => {
      fsFillEnabled.checked = false; applyFsFill();
      notify('Fill cleared (transparent)', 'ok');
    });
  }
  fsPanel.querySelector('#fs-sel-deselect').addEventListener('click', () => app.deselectLine());
}
