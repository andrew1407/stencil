// The "⋯" a held project row brings to the projects window's title: hovered, it opens that row's
// own menu, which highlights the item under the pointer as a hover would, and runs it only when
// the drag is released over it, as a release-pick menu does. A drag fires no hover, so every hover
// here is read off the pointer's coordinates. Desktop twin: dialogs/projects/list/ProjectDragMenu.cpp.
import { pointInRect } from '../../../utils.js';
import { icon } from '../../icons.js';
import { surfaceIn, surfaceOut, rectCenter, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from '../../motion.js';

// px of slack around the ⋯ and each open list: the gap between them is not a way out.
export const DRAG_MENU_SLACK_PX = 8;
const MENU_GAP_PX = 6;   // the row menu's own gap under its anchor

const near = (el, x, y, pad = 0) => {
  const r = el?.getBoundingClientRect?.();
  if (!r || !(r.width > 0)) return false;
  return pointInRect(x, y, { left: r.left - pad, top: r.top - pad, right: r.right + pad, bottom: r.bottom + pad });
};

export function createDragMenu({ title, showMenu, closeMenu }) {
  let btn = null;
  let items = null;
  let open = false;
  let hovered = null;
  let applied = false;

  // The menu and its flyout, the flyout last: on top where the two meet.
  const lists = () => [...document.querySelectorAll('.project-menu')];
  const itemAt = (x, y) => {
    for (const menu of lists().reverse())
      for (const item of menu.querySelectorAll('.project-menu-item')) if (near(item, x, y)) return item;
    return null;
  };

  const hover = (item) => {
    if (item === hovered) return;
    hovered?.classList.remove('is-drag-hover');
    hovered = item;
    if (!item) return;
    item.classList.add('is-drag-hover');
    // What a real hover does: a flyout item opens its list, any other item folds it.
    item.dispatchEvent(new MouseEvent('mouseenter'));
  };
  const close = () => {
    hover(null);
    if (!open) return;
    open = false;
    btn?.classList.remove('is-open');
    closeMenu();
  };

  // Whether (x, y) is the ⋯'s or its menu's: a drop there is theirs, never a zone's or a row's.
  const track = (x, y) => {
    if (!btn || applied) return false;
    const onBtn = near(btn, x, y, DRAG_MENU_SLACK_PX);
    if (!onBtn && !(open && lists().some((m) => near(m, x, y, DRAG_MENU_SLACK_PX)))) {
      close();
      return false;
    }
    if (onBtn && !open) {
      open = true;
      btn.classList.add('is-open');
      const r = btn.getBoundingClientRect();
      // Placed under the ⋯'s left edge, its dust out of (and back into) the ⋯'s centre.
      showMenu(btn, items(), { x: r.left, y: r.bottom + MENU_GAP_PX }, { from: rectCenter(btn) });
    }
    hover(open ? itemAt(x, y) : null);
    return true;
  };

  return {
    // `rowItems` builds the held row's own menu; a row without one brings no ⋯.
    begin(rowItems) {
      applied = false;
      items = rowItems || null;
      if (!items || !title?.parentElement) return;
      btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'projects-drag-more btn-icon';
      btn.setAttribute('aria-label', 'More actions for the dragged project');
      btn.innerHTML = icon('more', { size: 15 });
      title.insertAdjacentElement('afterend', btn);
      surfaceIn(btn, rectCenter(btn), { ms: SURFACE_MENU_IN_MS });
    },
    track,
    // Released on an item it runs, as its click; on the ⋯ or a flyout's opener nothing runs.
    drop(x, y) {
      if (!track(x, y)) return false;
      const item = open ? itemAt(x, y) : null;
      if (item && !item.classList.contains('has-sub')) {
        hover(null);
        applied = true;
        open = false;
        item.click();
      }
      return true;
    },
    end() {
      if (applied) hover(null); else close();
      if (btn) { surfaceOut(btn, rectCenter(btn), { ms: SURFACE_MENU_OUT_MS }); btn.remove(); }
      btn = null;
      items = null;
    },
    get applied() { return applied; },
  };
}
