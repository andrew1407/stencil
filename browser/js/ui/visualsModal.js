import { StencilElement, hostTag, define, wireModalShell, attachSearchFilter, rowMatches } from './base.js';
import { setVal, setRadioGroup, notify } from '../utils.js';
import { DEFAULT_ACCENT } from '../core/accents.js';
import { buildAccentPicker } from './accentPicker.js';
import { enhanceSelect } from './customSelect.js';
import { MOTION_MODE_LABELS, motionPrefs, MOTION_EVENT,
         DEFAULT_MOTION_MODE, DEFAULT_DRAWING_ANIMATIONS } from './motionPrefs.js';
import { icon } from './icons.js';
// ── Component: visual defaults modal ────────────────────────────
export class StencilVisualsModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('palette', { size: 18 })} Style &amp; Visual Settings</h2>
                <button class="app-modal-close btn-icon-text" id="visuals-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="modal-search-bar">
                <input type="text" id="vs-search" class="modal-search" placeholder="Search settings…">
            </div>
            <div class="settings-body">
                <div class="vs-section">App appearance</div>
                <div class="vs-row"><label>Main theme</label>
                    <div id="vs-accent" class="vs-ctrl"></div>
                </div>
                <div class="vs-row"><label>Appearance</label>
                    <span class="vs-ctrl"><select id="vs-appearance">
                        <option value="system">System (follow the OS)</option>
                        <option value="light">Light</option>
                        <option value="dark">Dark</option>
                    </select></span>
                </div>
                <div class="vs-section">Motion</div>
                <div class="vs-row"><label>Drawing animation</label>
                    <span class="vs-ctrl vs-ctrl-check"><input type="checkbox" id="vs-draw-anim"></span>
                </div>
                <div class="vs-row"><label>Interface animation</label>
                    <span class="vs-ctrl"><select id="vs-motion-mode">
                        ${MOTION_MODE_LABELS.map(([v, label]) => `<option value="${v}">${label}</option>`).join('')}
                    </select></span>
                </div>
                <div class="vs-section">Drawing defaults (applied to new lines)</div>
                <div class="vs-row"><label>Line color</label><label class="vs-ctrl vs-color"><input type="color" id="vs-line-color"><span class="vs-hex"></span></label></div>
                <div class="vs-row"><label>Line thickness</label><span class="vs-ctrl"><input type="number" id="vs-thickness" min="1" max="20"></span></div>
                <div class="vs-row"><label>Point size</label><span class="vs-ctrl"><input type="number" id="vs-point" min="1" max="30"></span></div>
                <div class="vs-row"><label>Line style</label>
                    <span class="vs-ctrl"><select id="vs-style"><option value="solid">Solid</option><option value="dashed">Dashed</option><option value="dotted">Dotted</option></select></span>
                </div>
                <div class="vs-row"><label>Area fill (new locked areas)</label><label class="vs-ctrl vs-color"><input type="color" id="vs-fill"><span class="vs-hex"></span></label></div>
                <div class="vs-section">Drawing behavior</div>
                <div class="vs-row"><label>Hold-to-draw delay (ms)</label><span class="vs-ctrl"><input type="number" id="vs-hold-delay" min="100" max="3000" step="50"></span></div>
                <div class="vs-section">Highlight styles</div>
                <div class="vs-row"><label>Selected line/point glow</label><label class="vs-ctrl vs-color"><input type="color" id="vs-sel-glow"><span class="vs-hex"></span></label></div>
                <div class="vs-row"><label>Point hover ring</label><label class="vs-ctrl vs-color"><input type="color" id="vs-hover-ring"><span class="vs-hex"></span></label></div>
                <div class="vs-row"><label>Point focus ring</label><label class="vs-ctrl vs-color"><input type="color" id="vs-focus-ring"><span class="vs-hex"></span></label></div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">Changes apply live and are saved automatically.</span>
                <button id="vs-reset" class="btn-icon-text">${icon('rotate-ccw', { size: 14 })}<span>Reset All</span></button>
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-visuals-modal', 'id="visuals-modal-overlay" class="app-modal-overlay"', StencilVisualsModal.inner()); }

  wire(app) {
    const overlay = document.getElementById('visuals-modal-overlay');
    const openBtn = document.getElementById('visuals-btn');
    const closeBtn = document.getElementById('visuals-close');
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
      color: '#FFFF00', thickness: 2, pointSize: 4, style: 'solid',
      defaultFillColor: '#ffffff', selGlowColor: '#ffc800',
      hoverRingColor: '#7c3aed', focusRingColor: '#7c3aed', holdDrawDelay: 500
    };

    // Main theme — a custom colour-swatch dropdown (./accentPicker.js).
    const accentMount = document.getElementById('vs-accent');
    const accentPicker = buildAccentPicker(accentMount, {
      current: app.customAccent || app.accent,
      // A #hex value means a custom colour → setCustomAccent; a preset key → setAccent.
      // The picker is passed as the swap origin: this dialog sits over the toolbar, so the
      // palette should flood out of the control under the cursor, not the icon behind it.
      onSelect: (key) => {
        const from = accentMount.querySelector('.accent-dd-trigger') || accentMount;
        return /^#/.test(key) ? app.setCustomAccent(key, from) : app.setAccent(key, from);
      },
    });
    // Another tab changed the accent — keep this picker's swatch in sync (the app
    // UI itself is already repainted by the cross-tab listener in drawingApp). Prefer the
    // custom hex so the trigger shows "Custom" rather than a stale preset name.
    window.addEventListener('stencil:accent-changed',
      () => accentPicker.set(app.customAccent || app.accent));

    // Appearance — light, dark, or SYSTEM (follow the OS; the toolbar moon/sun is the
    // quick flip). Mirrors the extension's options page and the desktop's themeMode.
    // The select is the swap origin, so the palette floods out of the touched control.
    const appearance = document.getElementById('vs-appearance');
    // Our own list, not the OS's — a native <select> popup is drawn by the platform and
    // ignores the app's theme entirely (ui/customSelect.js).
    enhanceSelect(appearance);
    const syncAppearance = () => { appearance.value = app.accents.themeMode; };
    syncAppearance();
    appearance.addEventListener('change', () => {
      const from = appearance.closest('.accent-dd')?.querySelector('.accent-dd-trigger') || appearance;
      app.accents.setThemeMode(appearance.value, from);
    });
    // The toolbar toggle (or another tab) can move it while this dialog is open.
    window.addEventListener('stencil:theme-changed', syncAppearance);

    // ── Motion: the canvas stroke animation, and how the interface itself moves ──
    // Both are app-wide (ui/motionPrefs.js), not part of the project — like the theme
    // above — and both route through the shared setter the console facade uses.
    const drawAnim = document.getElementById('vs-draw-anim');
    const motionMode = document.getElementById('vs-motion-mode');
    enhanceSelect(motionMode);
    const syncMotion = () => {
      const m = motionPrefs();
      drawAnim.checked = m.drawing;
      motionMode.value = m.mode;
    };
    syncMotion();
    drawAnim.addEventListener('change', () => app.settings.setMotion('drawing', drawAnim.checked));
    motionMode.addEventListener('change', () => app.settings.setMotion('mode', motionMode.value));
    // Moved from the console (or another dialog) while this one is open.
    window.addEventListener(MOTION_EVENT, syncMotion);

    const els = {
      lineColor: document.getElementById('vs-line-color'),
      thickness: document.getElementById('vs-thickness'),
      point: document.getElementById('vs-point'),
      style: document.getElementById('vs-style'),
      fill: document.getElementById('vs-fill'),
      selGlow: document.getElementById('vs-sel-glow'),
      hoverRing: document.getElementById('vs-hover-ring'),
      focusRing: document.getElementById('vs-focus-ring'),
      holdDelay: document.getElementById('vs-hold-delay')
    };

    // The colour wells read their hex beside the chip (desktop parity), kept in step.
    const syncHex = (input) => {
      const hex = input.parentElement?.querySelector('.vs-hex');
      if (hex) hex.textContent = String(input.value || '').toUpperCase();
    };
    const wells = [els.lineColor, els.fill, els.selGlow, els.hoverRing, els.focusRing];
    wells.forEach(w => w.addEventListener('input', () => syncHex(w)));

    // Line style takes the same themed dropdown as Appearance.
    enhanceSelect(els.style);

    const populate = () => {
      accentPicker.set(app.customAccent || app.accent);
      els.lineColor.value = app.color;
      els.thickness.value = app.thickness;
      els.point.value = app.pointSize;
      els.style.value = app.style;
      els.holdDelay.value = app.holdDrawDelay ?? VIS_DEFAULTS.holdDrawDelay;
      els.fill.value = app.defaultFillColor || VIS_DEFAULTS.defaultFillColor;
      els.selGlow.value = app.selGlowColor   || VIS_DEFAULTS.selGlowColor;
      els.hoverRing.value = app.hoverRingColor || VIS_DEFAULTS.hoverRingColor;
      els.focusRing.value = app.focusRingColor || VIS_DEFAULTS.focusRingColor;
      wells.forEach(syncHex);
    };

    // Default-line controls mirror the main toolbar inputs
    els.lineColor.addEventListener('input', e => {
      app.color = e.target.value;
      setVal('line-color', e.target.value);
      app.storage.save();
    });
    els.thickness.addEventListener('change', e => {
      app.thickness = Math.max(1, Math.min(20, parseInt(e.target.value) || app.thickness));
      e.target.value = app.thickness;
      setVal('line-thickness', app.thickness);
      app.storage.save();
    });
    els.point.addEventListener('change', e => {
      app.pointSize = Math.max(1, Math.min(30, parseInt(e.target.value) || app.pointSize));
      e.target.value = app.pointSize;
      setVal('point-size', app.pointSize);
      app.renderer.redraw(); app.storage.save();
    });
    els.style.addEventListener('change', e => {
      app.style = e.target.value;
      setVal('line-style', e.target.value);
      setRadioGroup('ctxLineStyle', e.target.value);
      app.storage.save();
    });
    els.holdDelay.addEventListener('change', e => {
      app.input.setHoldDrawDelay(e.target.value);
      e.target.value = app.holdDrawDelay; // reflect the clamped value
    });
    // Shared core setter (also used by the console: stencil.settings.fillColor, etc.)
    els.fill.addEventListener('input', e => app.settings.setVisualColor('fill', e.target.value));
    els.selGlow.addEventListener('input', e => app.settings.setVisualColor('selGlow', e.target.value));
    els.hoverRing.addEventListener('input', e => app.settings.setVisualColor('hoverRing', e.target.value));
    els.focusRing.addEventListener('input', e => app.settings.setVisualColor('focusRing', e.target.value));

    resetBtn.addEventListener('click', () => {
      Object.assign(app, VIS_DEFAULTS);
      app.setAccent(DEFAULT_ACCENT);
      app.settings.setMotion('drawing', DEFAULT_DRAWING_ANIMATIONS);
      app.settings.setMotion('mode', DEFAULT_MOTION_MODE);
      setVal('line-color', app.color);
      setVal('line-thickness', app.thickness);
      setVal('point-size', app.pointSize);
      setVal('line-style', app.style);
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
