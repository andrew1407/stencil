import { surfaceIn, surfaceOut, centerOf } from '../motion.js';
import { icon } from '../icons.js';
import { createAccentPreview } from './accentPreview.js';
import { attachMenuScrollbar } from '../control/menuScrollbar.js';

export function wireLogoAccent(logo) {
  const A = typeof window !== 'undefined' && window.StencilAccent;
  if (!logo || !A) return;
  const wrap = logo.closest?.('.logo-wrap') || logo;
  logo.style.cursor = 'pointer';

  let menu = wrap.querySelector?.('.logo-accent-menu');
  if (!menu) {
    menu = document.createElement('ul');
    menu.className = 'accent-dd-menu logo-accent-menu';
    menu.setAttribute('role', 'listbox');
    menu.setAttribute('aria-label', 'Color theme');
    menu.hidden = true;
    wrap.appendChild(menu);
  }
  const { clearHover, markSel, onRowEnter, pick, restore, scheduleRestore } =
    createAccentPreview({ menu, logo, accent: A, closeMenu: () => closeMenu() });

  const fill = () => {
    if (menu.childElementCount) return;
    for (const a of A.list) {
      const li = document.createElement('li');
      li.className = 'accent-dd-opt';
      li.setAttribute('role', 'option');
      li.dataset.key = a.key;
      li.innerHTML =
        `<span class="accent-swatch" style="background:${a.hex};color:${A.inkOn(a.hex)}">` +
        `${icon('check', { size: 11, cls: 'accent-check', sw: 3.5 })}</span>` +
        `<span class="accent-dd-name">${a.label}</span>`;
      li.addEventListener('click', () => pick(a.key));
      li.addEventListener('pointerenter', () => onRowEnter(li, a.key));
      li.addEventListener('pointerleave', clearHover);
      menu.appendChild(li);
    }
    // A synthetic mid-flood leave, or a hop between rows, must not revert.
    menu.addEventListener('pointerleave', scheduleRestore);
  };
  const menuShowing = () => !menu.hidden;
  const onDocDown = (e) => { if (!wrap.contains(e.target)) closeMenu(); };
  const onKey = (e) => { if (e.key === 'Escape') closeMenu(); };
  const openMenu = () => {
    fill();
    markSel();
    // The space under the logo overrides the shared 280px .accent-dd-menu cap (browser twin: toolbar.js).
    const r = wrap.getBoundingClientRect?.();
    if (r && typeof window !== 'undefined' && typeof window.innerHeight === 'number')
      menu.style.maxHeight = `${Math.max(120, window.innerHeight - r.bottom - 16)}px`;
    menu.hidden = false;
    attachMenuScrollbar(menu);   // measurable only once shown
    wrap.classList.add('logo-menu-open');   // lifts the badge's stacking context over the page
    surfaceIn(menu, centerOf(wrap));
    document.addEventListener('pointerdown', onDocDown, true);
    document.addEventListener('keydown', onKey);
  };
  const closeMenu = () => {
    if (menu.hidden) return;
    restore();
    surfaceOut(menu, centerOf(wrap));
    menu.hidden = true;
    wrap.classList.remove('logo-menu-open');
    document.removeEventListener('pointerdown', onDocDown, true);
    document.removeEventListener('keydown', onKey);
  };
  wrap.addEventListener('contextmenu', (e) => { e.preventDefault(); menuShowing() ? closeMenu() : openMenu(); });
  wrap.addEventListener('mouseenter', (e) => { if (e.altKey && !menuShowing()) openMenu(); });
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Alt' && wrap.matches?.(':hover') && !menuShowing()) { e.preventDefault?.(); openMenu(); }
  });

  const cycle = () => {
    const keys = A.list.map((a) => a.key);
    const i = keys.indexOf(A.get());
    A.set(keys[(i + 1) % keys.length], logo);
    markSel();
  };
  let clickTimer = null;
  logo.addEventListener('click', (e) => {
    if (e.altKey) {
      if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }
      menuShowing() ? closeMenu() : openMenu();
      return;
    }
    if (clickTimer) return;   // the second click of a double — dblclick handles it
    clickTimer = setTimeout(() => { clickTimer = null; cycle(); }, 220);
  });

  // The native picker needs a real, rendered element (not display:none) to open from.
  const picker = document.createElement('input');
  picker.type = 'color';
  picker.setAttribute('aria-hidden', 'true');
  picker.tabIndex = -1;
  picker.style.cssText = 'position:absolute;width:1px;height:1px;opacity:0;border:0;padding:0;pointer-events:none;';
  logo.insertAdjacentElement('afterend', picker);
  const applyCustom = () => A.setCustom(picker.value, logo);
  picker.addEventListener('input', applyCustom);
  picker.addEventListener('change', applyCustom);
  logo.addEventListener('dblclick', () => {
    if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }
    const cur = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim();
    picker.value = /^#[0-9a-fA-F]{6}$/.test(cur) ? cur : A.hexOf(A.get());
    try { if (typeof picker.showPicker === 'function') picker.showPicker(); else picker.click(); }
    catch { picker.click(); }
  });
  // A double-click would select nearby text.
  logo.addEventListener('mousedown', (e) => { if (e.detail > 1) e.preventDefault(); });
}
