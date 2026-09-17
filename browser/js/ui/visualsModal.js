import { StencilElement, hostTag, define, wireModalShell, attachSearchFilter, rowMatches } from './base.js';
import { visualsModalInner } from './visualsMarkup.js';
import { setVal, setRadioGroup, notify } from '../utils.js';
import { DEFAULT_ACCENT } from '../core/accents.js';
import { buildAccentPicker } from './accentPicker.js';
import { motionModeIcon } from './motionIcons.js';
import { enhanceSelect } from './customSelect.js';
import { motionPrefs, MOTION_EVENT, DEFAULT_MOTION_MODE,
         DEFAULT_DRAWING_ANIMATIONS, DEFAULT_MODAL_BACKDROP } from './motionPrefs.js';
import { subscribe, EVENTS } from '../eventBus/appBus.js';
// ── Component: visual defaults modal ────────────────────────────
export class StencilVisualsModal extends StencilElement {
  static inner() { return visualsModalInner(); }
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
      // Resting on a preset row previews it on the page — with the flood a real change
      // plays, out of this picker — and leaving/closing without a pick floods back to the
      // committed accent (accentController).
      preview: {
        on: (key) => app.previewAccent?.(key, accentMount.querySelector('.accent-dd-trigger') || accentMount),
        off: () => app.endAccentPreview?.(accentMount.querySelector('.accent-dd-trigger') || accentMount),
      },
    });
    // The accent moved elsewhere (logo click-cycle or menu, another tab) — keep this picker's
    // swatch in sync. Custom hex first, so the trigger never shows a stale preset name.
    subscribe(EVENTS.accentChanged,
      (e) => accentPicker.set(typeof e.detail === 'string' ? e.detail : (app.customAccent || app.accent)));

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
    subscribe(EVENTS.themeChanged, syncAppearance);

    // ── Motion: the stroke animation, the window backdrop, and how the interface moves ──
    // All app-wide (ui/motionPrefs.js), not the project's, and all routed through the
    // shared setter the console facade uses.
    const motionMode = document.getElementById('vs-motion-mode');
    // Enhanced HERE, with each mode's glyph (animated on hover), not by the app-wide pass
    // — data-cs-skip on the <select> keeps that pass off it, or its plain rows would win.
    enhanceSelect(motionMode, { icons: motionModeIcon });
    const checks = [['vs-draw-anim', 'drawing'], ['vs-modal-backdrop', 'backdrop']].map(([id, k]) => [document.getElementById(id), k]);
    const syncMotion = () => {
      const m = motionPrefs();
      for (const [box, key] of checks) box.checked = m[key];
      motionMode.value = m.mode;
    };
    syncMotion();
    for (const [box, key] of checks) box.addEventListener('change', () => app.settings.setMotion(key, box.checked));
    motionMode.addEventListener('change', () => app.settings.setMotion('mode', motionMode.value));
    // Moved from the console (or another dialog) while this one is open.
    subscribe(MOTION_EVENT, syncMotion);

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
      syncMotion();
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
      app.thickness = Math.max(1, Math.min(20, parseInt(e.target.value, 10) || app.thickness));
      e.target.value = app.thickness;
      setVal('line-thickness', app.thickness);
      app.storage.save();
    });
    els.point.addEventListener('change', e => {
      app.pointSize = Math.max(1, Math.min(30, parseInt(e.target.value, 10) || app.pointSize));
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
      const motionDefaults = [['drawing', DEFAULT_DRAWING_ANIMATIONS], ['backdrop', DEFAULT_MODAL_BACKDROP], ['mode', DEFAULT_MOTION_MODE]];
      for (const [k, v] of motionDefaults) app.settings.setMotion(k, v);
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
