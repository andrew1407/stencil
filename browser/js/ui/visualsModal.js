import { StencilElement, hostTag, define, wireModalShell, attachSearchFilter, rowMatches } from './base.js';
import { setVal, setRadioGroup, notify } from '../utils.js';
// ── Component: visual defaults modal ────────────────────────────
export class StencilVisualsModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>🎨 Default Visuals</h2>
                <button class="app-modal-close" id="visualsClose">✕ Close</button>
            </div>
            <div class="modal-search-bar">
                <input type="text" id="vs-search" class="modal-search" placeholder="Search settings…">
            </div>
            <div class="settings-body">
                <div class="vs-section">Drawing defaults (applied to new lines)</div>
                <div class="vs-row"><label>Line color</label><input type="color" id="vs-line-color"></div>
                <div class="vs-row"><label>Line thickness</label><input type="number" id="vs-thickness" min="1" max="20"></div>
                <div class="vs-row"><label>Marker size</label><input type="number" id="vs-marker" min="1" max="30"></div>
                <div class="vs-row"><label>Line style</label>
                    <select id="vs-style"><option value="solid">Solid</option><option value="dashed">Dashed</option><option value="dotted">Dotted</option></select>
                </div>
                <div class="vs-row"><label>Area fill (new locked areas)</label><input type="color" id="vs-fill"></div>
                <div class="vs-section">Highlight styles</div>
                <div class="vs-row"><label>Selected line/point glow</label><input type="color" id="vs-sel-glow"></div>
                <div class="vs-row"><label>Point hover ring</label><input type="color" id="vs-hover-ring"></div>
                <div class="vs-row"><label>Point focus ring</label><input type="color" id="vs-focus-ring"></div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">Changes apply live and are saved automatically.</span>
                <button id="vs-reset">↺ Reset All</button>
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-visuals-modal', 'id="visualsModalOverlay" class="app-modal-overlay"', StencilVisualsModal.inner()); }

  wire(app) {
    const overlay = document.getElementById('visualsModalOverlay');
    const openBtn = document.getElementById('visualsBtn');
    const closeBtn = document.getElementById('visualsClose');
    const resetBtn = document.getElementById('vs-reset');
    const search = document.getElementById('vs-search');
    const bodyEl = overlay.querySelector('.settings-body');

    // Filter rows by their label; hide section headers whose rows all hid.
    const emptyMsg = document.createElement('div');
    emptyMsg.className = 'info-empty';
    emptyMsg.textContent = 'No matching settings.';
    emptyMsg.style.display = 'none';
    bodyEl.appendChild(emptyMsg);
    const applyFilter = () => {
      const q = search.value || '';
      let any = false, section = null, sectionMatch = false;
      const flush = () => { if (section) section.style.display = sectionMatch ? '' : 'none'; };
      for (const el of bodyEl.children) {
        if (el.classList.contains('vs-section')) { flush(); section = el; sectionMatch = false; }
        else if (el.classList.contains('vs-row')) {
          const label = el.querySelector('label')?.textContent || '';
          const match = rowMatches(label, q);
          el.style.display = match ? '' : 'none';
          if (match) { sectionMatch = true; any = true; }
        }
      }
      flush();
      emptyMsg.style.display = any ? 'none' : '';
    };
    attachSearchFilter(search, applyFilter);

    const VIS_DEFAULTS = {
      color: '#FFFF00', thickness: 2, markerSize: 4, style: 'solid',
      defaultFillColor: '#3399ff', selGlowColor: '#ffc800',
      hoverRingColor: '#007bff', focusRingColor: '#007bff'
    };

    const els = {
      lineColor: document.getElementById('vs-line-color'),
      thickness: document.getElementById('vs-thickness'),
      marker: document.getElementById('vs-marker'),
      style: document.getElementById('vs-style'),
      fill: document.getElementById('vs-fill'),
      selGlow: document.getElementById('vs-sel-glow'),
      hoverRing: document.getElementById('vs-hover-ring'),
      focusRing: document.getElementById('vs-focus-ring')
    };

    const populate = () => {
      els.lineColor.value = app.color;
      els.thickness.value = app.thickness;
      els.marker.value = app.markerSize;
      els.style.value = app.style;
      els.fill.value = app.defaultFillColor || VIS_DEFAULTS.defaultFillColor;
      els.selGlow.value = app.selGlowColor   || VIS_DEFAULTS.selGlowColor;
      els.hoverRing.value = app.hoverRingColor || VIS_DEFAULTS.hoverRingColor;
      els.focusRing.value = app.focusRingColor || VIS_DEFAULTS.focusRingColor;
    };

    // Default-line controls mirror the main toolbar inputs
    els.lineColor.addEventListener('input', e => {
      app.color = e.target.value;
      setVal('lineColor', e.target.value);
      app.storage.save();
    });
    els.thickness.addEventListener('change', e => {
      app.thickness = Math.max(1, Math.min(20, parseInt(e.target.value) || app.thickness));
      e.target.value = app.thickness;
      setVal('lineThickness', app.thickness);
      app.storage.save();
    });
    els.marker.addEventListener('change', e => {
      app.markerSize = Math.max(1, Math.min(30, parseInt(e.target.value) || app.markerSize));
      e.target.value = app.markerSize;
      setVal('markerSize', app.markerSize);
      app.renderer.redraw(); app.storage.save();
    });
    els.style.addEventListener('change', e => {
      app.style = e.target.value;
      setVal('lineStyle', e.target.value);
      setRadioGroup('ctxLineStyle', e.target.value);
      app.storage.save();
    });
    els.fill.addEventListener('input', e => { app.defaultFillColor = e.target.value; app.storage.save(); });
    els.selGlow.addEventListener('input', e => { app.selGlowColor = e.target.value; app.renderer.redraw(); app.storage.save(); });
    els.hoverRing.addEventListener('input', e => { app.hoverRingColor = e.target.value; app.renderer.redraw(); app.storage.save(); });
    els.focusRing.addEventListener('input', e => { app.focusRingColor = e.target.value; app.renderer.redraw(); app.storage.save(); });

    resetBtn.addEventListener('click', () => {
      Object.assign(app, VIS_DEFAULTS);
      setVal('lineColor', app.color);
      setVal('lineThickness', app.thickness);
      setVal('markerSize', app.markerSize);
      setVal('lineStyle', app.style);
      populate();
      app.renderer.redraw(); app.storage.save();
      notify('Visual defaults reset', 'ok');
    });

    wireModalShell(overlay, openBtn, closeBtn, {
      onOpen: () => { populate(); search.value = ''; applyFilter(); }
    });
  }
}
define('stencil-visuals-modal', StencilVisualsModal);
