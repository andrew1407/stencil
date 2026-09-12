import { surfaceIn, surfaceOut, centerOf } from './motion.js';
import { icon } from './icons.js';
import { createAccentPreview } from './accentPreview.js';

export function wireLogoAccent(logo) {
  const A = typeof window !== 'undefined' && window.StencilAccent;
  if (!logo || !A) return;
  const wrap = logo.closest?.('.logo-wrap') || logo;
  logo.style.cursor = 'pointer';

  // ── Preset menu ──
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
    // Leaving the list reverts — but only once the flood has settled and the pointer has
    // really gone (a synthetic mid-flood leave, or a hop between rows, must not).
    menu.addEventListener('pointerleave', scheduleRestore);
  };
  const menuShowing = () => !menu.hidden;
  const onDocDown = (e) => { if (!wrap.contains(e.target)) closeMenu(); };
  const onKey = (e) => { if (e.key === 'Escape') closeMenu(); };
  const openMenu = () => {
    fill();
    markSel();
    // Show the whole list when the window has room; only cap (and scroll) when it does
    // not — the space under the logo, overriding the shared 280px .accent-dd-menu cap.
    // Browser twin: toolbar.js openMenu sizes the logo menu the same way.
    const r = wrap.getBoundingClientRect?.();
    if (r && typeof window !== 'undefined' && typeof window.innerHeight === 'number')
      menu.style.maxHeight = `${Math.max(120, window.innerHeight - r.bottom - 16)}px`;
    menu.hidden = false;
    wrap.classList.add('logo-menu-open');   // lift the badge's stacking context over the page
    surfaceIn(menu, centerOf(wrap));   // pours out of the logo, like every extension surface
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

  // ── Click cycles; double-click opens the custom picker ──
  const cycle = () => {
    const keys = A.list.map((a) => a.key);
    const i = keys.indexOf(A.get());
    A.set(keys[(i + 1) % keys.length], logo);
    markSel();
  };
  let clickTimer = null;
  logo.addEventListener('click', (e) => {
    if (e.altKey) {   // Alt+click toggles the menu instead of cycling
      if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }
      menuShowing() ? closeMenu() : openMenu();
      return;
    }
    if (clickTimer) return;   // the second click of a double — dblclick handles it
    clickTimer = setTimeout(() => { clickTimer = null; cycle(); }, 220);
  });

  // A near-invisible colour input parked by the logo — the native picker needs a real,
  // rendered element (not display:none) to open from.
  const picker = document.createElement('input');
  picker.type = 'color';
  picker.setAttribute('aria-hidden', 'true');
  picker.tabIndex = -1;
  picker.style.cssText = 'position:absolute;width:1px;height:1px;opacity:0;border:0;padding:0;pointer-events:none;';
  logo.insertAdjacentElement('afterend', picker);
  const applyCustom = () => A.setCustom(picker.value, logo);
  picker.addEventListener('input', applyCustom);    // live while dragging
  picker.addEventListener('change', applyCustom);   // final commit
  logo.addEventListener('dblclick', () => {
    if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }   // cancel the pending cycle
    const cur = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim();
    picker.value = /^#[0-9a-fA-F]{6}$/.test(cur) ? cur : A.hexOf(A.get());
    try { if (typeof picker.showPicker === 'function') picker.showPicker(); else picker.click(); }
    catch { picker.click(); }
  });
  // A double-click selects nearby text; clear it so the picker isn't fighting a selection.
  logo.addEventListener('mousedown', (e) => { if (e.detail > 1) e.preventDefault(); });
}
