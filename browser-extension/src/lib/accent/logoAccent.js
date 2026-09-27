import { surfaceIn, surfaceOut, centerOf } from '../motion.js';
import { icon } from '../icons.js';
import { createAccentPreview } from './preview.js';
import { attachMenuScrollbar } from '../control/menuScrollbar.js';
import { createModalOpenGesture } from '../tip/popover.js';
import { growFrom } from '../control/dropdownMenu.js';
import { onAltKeys, pointerIn, wirePeekBox, wireReleasePick } from '../tip/altPeek.js';

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
    createAccentPreview({ menu, logo, accent: A, closeMenu: () => { if (!release.picking()) closeMenu(); } });

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
  // 'peek' (Alt-opened) or 'sticky' (right-click); only the Alt+click no-op needs it.
  let kind = null;
  let pending = null;
  const onDocDown = (e) => { if (!wrap.contains(e.target)) closeMenu(); };
  const onKey = (e) => { if (e.key === 'Escape') closeMenu(); };
  const openMenu = () => {
    if (pending) kind = pending;
    fill();
    markSel();
    // The space under the logo overrides the shared 280px .accent-dd-menu cap (browser twin: toolbar.js).
    const r = wrap.getBoundingClientRect?.();
    if (r && typeof window !== 'undefined' && typeof window.innerHeight === 'number')
      menu.style.maxHeight = `${Math.max(120, window.innerHeight - r.bottom - 16)}px`;
    menu.hidden = false;
    const logoPoint = centerOf(wrap);
    growFrom(menu, logoPoint);   // the slide grows out of the logo, as the motes do
    attachMenuScrollbar(menu);   // measurable only once shown
    wrap.classList.add('logo-menu-open');   // lifts the badge's stacking context over the page
    surfaceIn(menu, logoPoint);
    document.addEventListener('pointerdown', onDocDown, true);
    document.addEventListener('keydown', onKey);
  };
  const closeMenu = () => {
    if (menu.hidden) return;
    kind = null;
    g.notifyClosed();
    restore();
    surfaceOut(menu, centerOf(wrap));
    menu.hidden = true;
    wrap.classList.remove('logo-menu-open');
    document.removeEventListener('pointerdown', onDocDown, true);
    document.removeEventListener('keydown', onKey);
  };
  // The mini-window machine (tip/popover.js), as the editor's logo menu: Alt+hover peeks,
  // Alt released over the list lingers until the pointer leaves it; a right-click is sticky.
  const g = createModalOpenGesture({
    openFull: () => {},
    openPopover: openMenu,
    closePopover: closeMenu,
    isPopoverOpen: menuShowing,
    isPeekEngaged: () => pointerIn(menu),
  });
  const altPeek = () => { pending = 'peek'; g.altHover(wrap); pending = null; };
  wrap.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    if (menuShowing()) { closeMenu(); return; }
    pending = 'sticky'; g.contextmenu(); pending = null;
  });
  wrap.addEventListener('mouseenter', (e) => { if (e.altKey) altPeek(); });
  onAltKeys({
    press: (e) => { if (!e.repeat && wrap.matches?.(':hover')) { e.preventDefault?.(); altPeek(); } },
  });
  // A peek released on a colour picks it and stays up, lingering like any peek released inside;
  // its release also answers a blur (Alt+Tab switches away without delivering the keyup).
  const release = wireReleasePick(menu, g, '.accent-dd-opt',
                                  { isPeek: () => kind === 'peek', isShowing: menuShowing });
  // A flood's synthetic leave lands on a lingering menu with every preview and pick: the
  // flood-safe watcher waits the swap out, then the pointer's real place decides.
  wirePeekBox(menu, g);

  const cycle = () => {
    const keys = A.list.map((a) => a.key);
    const i = keys.indexOf(A.get());
    A.set(keys[(i + 1) % keys.length], logo);
    markSel();
  };
  let clickTimer = null;
  logo.addEventListener('click', (e) => {
    // An Alt-opened menu treats the click as part of the hold; a sticky one toggles closed.
    if (e.altKey) {
      if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }
      if (menuShowing()) { if (kind !== 'peek') closeMenu(); return; }
      altPeek();
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
