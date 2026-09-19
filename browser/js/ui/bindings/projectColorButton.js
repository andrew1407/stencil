import { anchorPickerInput } from '../../utils.js';
import { surfaceIn, surfaceOut, rectCenter, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from '../motion.js';
import { icon } from '../icons.js';
import { normalizeHex } from '../../core/accents.js';
export function wireProjectColorButton(app) {
  // Project-colour swatch: a native colour picker that paints the project NAME. Live while
  // dragging; a right-click (or holding Alt at open) clears the colour back to the theme accent.
  const colorBtn = document.getElementById('project-color-btn');
  const colorInput = document.getElementById('project-color-input');
  if (colorBtn && colorInput) {
    const apply = () => {
      if (app.activeProjectId != null) app.setProjectColor(app.activeProjectId, colorInput.value);
    };
    colorInput.addEventListener('input', apply);
    colorInput.addEventListener('change', apply);
    const openPicker = () => {
      const cur = app.storage.store.getMeta(app.activeProjectId)?.color || '';
      // No custom colour → open at the neutral grey the name is actually painted in (the unset
      // default), not the theme accent, so the picker reflects the real current state.
      colorInput.value = normalizeHex(cur) || '#80868f';
      anchorPickerInput(colorInput, colorBtn);
      try {
        if (typeof colorInput.showPicker === 'function') colorInput.showPicker();
        else colorInput.click();
      } catch {
        colorInput.click();
      }
    };
    colorBtn.addEventListener('click', e => {
      if (app.activeProjectId == null || app.storage.incognito) return;
      e.stopPropagation();
      // The menu is sand like every other popup (projects kebab parity): it forms out
      // of the button and pours back into it, whichever way it is dismissed.
      const btnPoint = () => rectCenter(colorBtn);
      const openMenu = document.getElementById('project-color-menu');
      if (openMenu) {   // toggle off — through its own close(), so the doc listener goes too
        openMenu.__close?.();
        return;
      }
      // No custom colour → there is nothing to clear, and a one-row menu is a detour:
      // straight into the picker (desktop showProjectColorMenu parity).
      if (!app.storage.store.getMeta(app.activeProjectId)?.color) {
        openPicker();
        return;
      }
      const menu = document.createElement('div');
      menu.id = 'project-color-menu';
      menu.className = 'project-menu';
      const close = () => {
        surfaceOut(menu, btnPoint(), { ms: SURFACE_MENU_OUT_MS });
        menu.remove();
        document.removeEventListener('mousedown', onDoc, true);
      };
      menu.__close = close;
      const item = (ic, label, onClick) => {
        const b = document.createElement('button');
        b.className = 'project-menu-item btn-icon-text';
        b.innerHTML = `${icon(ic, { size: 15 })}<span>${label}</span>`;
        b.addEventListener('click', ev => { ev.stopPropagation(); close(); onClick(); });
        menu.appendChild(b);
      };
      // The menu shows only WITH a custom colour set (the guard above), so the clear
      // row is always meaningful here.
      item('palette', 'Choose color…', openPicker);
      item('x', 'Default (no color)', () => app.setProjectColor(app.activeProjectId, ''));
      document.body.appendChild(menu);
      const r = colorBtn.getBoundingClientRect();
      const mw = menu.offsetWidth;
      menu.style.left = `${Math.max(8, Math.min(r.right - mw, window.innerWidth - mw - 8))}px`;
      menu.style.top = `${r.bottom + 6}px`;
      surfaceIn(menu, btnPoint(), { ms: SURFACE_MENU_IN_MS });
      const onDoc = ev => { if (!menu.contains(ev.target)) close(); };
      setTimeout(() => document.addEventListener('mousedown', onDoc, true), 0);
    });
    colorBtn.addEventListener('contextmenu', e => {
      e.preventDefault();
      if (app.activeProjectId != null) app.setProjectColor(app.activeProjectId, '');
    });
  }
}
