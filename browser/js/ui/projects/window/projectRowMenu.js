// The per-row "⋯" menu, one floating node reused by every row. Closes on click-away,
// Escape, or re-render (the list calls closeMenu() before it rebuilds).
import { SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from '../../motion/surface/motion.js';
import { popupIn, popupOut } from '../../motion/control/popup.js';
import { rectCenter } from '../../motion/control/tips.js';
import { menuItemButton, createRowSubmenu, wireActionItem } from './rowSubmenu.js';

export const createProjectRowMenu = () => {
  let openMenu = null;
  let openAnchor = null;   // the control it belongs to: pressing it again closes, never reopens
  let openByButton = false;
  let menuPoint = null;
  let subs = [];
  const closeMenu = () => {
    if (!openMenu) return;
    for (const s of subs) s.hide();
    subs = [];
    // Back into that point as dust or, Sliding, a genie in its own layer; the node still goes NOW.
    popupOut(openMenu, menuPoint, SURFACE_MENU_OUT_MS);
    openMenu.remove();
    openMenu = openAnchor = null;
    document.removeEventListener('mousedown', onMenuDocDown, true);
    document.removeEventListener('keydown', onMenuKey, true);
  };
  const onMenuDocDown = e => {
    if (!openMenu || openMenu.contains(e.target) || subs.some((s) => s.contains(e.target))) return;
    if (!openAnchor?.contains?.(e.target)) closeMenu();   // its own control's click toggles it shut
  };
  const onMenuKey = e => { if (e.key === 'Escape') { e.stopPropagation(); closeMenu(); } };
  // Opens under `anchor` (the "⋯" button), or at `point` ({x,y}) for a right-click; `from` is
  // where its dust gathers from and returns to when that is neither.
  const showMenu = (anchor, items, point = null, { from = null } = {}) => {
    // A menu its own button opened is toggled shut by it; a right-click (a point and no `from`) opens at
    // the cursor, and the button then opens it again from itself.
    const byButton = !point || !!from;
    const again = !!openMenu && openAnchor === anchor && openByButton && byButton;
    closeMenu();
    if (again) return;
    const menu = document.createElement('div');
    menu.className = 'project-menu';
    const run = (it, at) => { closeMenu(); it.onClick(at); };
    for (const it of items) {
      if (!it) continue;
      const b = menuItemButton(it);
      // A nested list flies out beside its item; moving onto any other item folds it.
      if (it.items) { subs.push(createRowSubmenu(b, it.items, run, () => subs)); menu.appendChild(b); continue; }
      b.addEventListener('mouseenter', () => { for (const s of subs) s.hide(); });
      // Each handler gets its row's rect, measured before the menu goes, so a window raised
      // from here grows out of the clicked row.
      wireActionItem(b, it, run);
      menu.appendChild(b);
    }
    document.body.appendChild(menu);
    const mw = menu.offsetWidth;
    const mh = menu.offsetHeight;
    let x;
    let y;
    if (point) {
      x = point.x + mw > window.innerWidth - 8 ? point.x - mw : point.x;
      y = point.y + mh > window.innerHeight - 8 ? point.y - mh : point.y;
    } else {
      const r = anchor.getBoundingClientRect();
      x = r.right - mw;
      y = r.bottom + 6;
      if (y + mh > window.innerHeight - 8) y = r.top - mh - 6;
    }
    menu.style.left = `${Math.max(8, x)}px`;
    menu.style.top = `${Math.max(8, y)}px`;
    openMenu = menu;
    openAnchor = anchor;
    openByButton = byButton;
    // The cursor for a right-click, the "⋯" button's centre otherwise.
    menuPoint = from || point || rectCenter(anchor);
    popupIn(menu, menuPoint, SURFACE_MENU_IN_MS);
    setTimeout(() => {
      document.addEventListener('mousedown', onMenuDocDown, true);
      document.addEventListener('keydown', onMenuKey, true);
    }, 0);
  };

  return { showMenu, closeMenu };
};
