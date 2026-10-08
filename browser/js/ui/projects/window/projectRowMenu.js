// The per-row "⋯" menu, one floating node reused by every row. Closes on click-away,
// Escape, or re-render (the list calls closeMenu() before it rebuilds).
import { surfaceIn, surfaceOut, rectCenter, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from '../../motion.js';
import { menuItemButton, createRowSubmenu } from './rowSubmenu.js';

export const createProjectRowMenu = () => {
  let openMenu = null;
  let menuPoint = null;
  let subs = [];
  const closeMenu = () => {
    if (!openMenu) return;
    for (const s of subs) s.hide();
    subs = [];
    // Back into that point as dust in its own layer, so the node still goes away NOW.
    surfaceOut(openMenu, menuPoint, { ms: SURFACE_MENU_OUT_MS });
    openMenu.remove();
    openMenu = null;
    document.removeEventListener('mousedown', onMenuDocDown, true);
    document.removeEventListener('keydown', onMenuKey, true);
  };
  const onMenuDocDown = e => {
    if (openMenu && !openMenu.contains(e.target) && !subs.some((s) => s.contains(e.target))) closeMenu();
  };
  const onMenuKey = e => { if (e.key === 'Escape') { e.stopPropagation(); closeMenu(); } };
  // Opens under `anchor` (the "⋯" button), or at `point` ({x,y}) for a right-click; `from` is
  // where its dust gathers from and returns to when that is neither.
  const showMenu = (anchor, items, point = null, { from = null } = {}) => {
    closeMenu();
    const menu = document.createElement('div');
    menu.className = 'project-menu';
    const run = (it, at) => { closeMenu(); it.onClick(at); };
    for (const it of items) {
      if (!it) continue;
      const b = menuItemButton(it);
      // A nested list flies out beside its item; moving onto any other item folds it.
      if (it.items) { subs.push(createRowSubmenu(b, it.items, run)); menu.appendChild(b); continue; }
      b.addEventListener('mouseenter', () => { for (const s of subs) s.hide(); });
      // Each handler gets its row's rect, measured before the menu goes, so a window raised
      // from here grows out of the clicked row.
      b.addEventListener('click', e => { e.stopPropagation(); run(it, b.getBoundingClientRect()); });
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
    // The cursor for a right-click, the "⋯" button's centre otherwise.
    menuPoint = from || point || rectCenter(anchor);
    surfaceIn(menu, menuPoint, { ms: SURFACE_MENU_IN_MS });
    setTimeout(() => {
      document.addEventListener('mousedown', onMenuDocDown, true);
      document.addEventListener('keydown', onMenuKey, true);
    }, 0);
  };

  return { showMenu, closeMenu };
};
