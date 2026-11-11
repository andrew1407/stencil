// A row menu item's nested list (projectRowMenu.js): a second `.project-menu` flown out beside
// its item — right, or left when the window has no room — on hover, click or ArrowRight, back
// on ArrowLeft / Escape; an item in it may nest again. It lives on <body> like its parent.
import { icon } from '../../icons.js';
import { popupIn, popupOut } from '../../motion/control/popup.js';
import { SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from '../../motion/surface/motion.js';
import { rectCenter } from '../../motion/control/tips.js';
import { clickOrDouble } from '../../../core/pointer/clickOrDouble.js';

const GAP = 4;
const EDGE = 8;

export const menuItemButton = (it) => {
  const b = document.createElement('button');
  // Not the global `.danger` (which fills the button red): red text on the menu background.
  b.className = 'project-menu-item btn-icon-text' + (it.danger ? ' is-danger' : '') + (it.items ? ' has-sub' : '');
  b.innerHTML = `${icon(it.icon, { size: 15 })}<span>${it.label}</span>`
    + (it.items ? icon('chevron-right', { size: 13, cls: 'project-menu-arrow' }) : '');
  return b;
};

// An action item's click: `pick` at once, or — with `onDoubleClick` — once the double-click window
// passes, a second click inside it picking the double instead. Each gets the row's rect.
export const wireActionItem = (b, it, pick) => {
  const at = () => b.getBoundingClientRect();
  const run = it.onDoubleClick
    ? clickOrDouble({ single: () => pick(it, at()), double: () => pick({ ...it, onClick: it.onDoubleClick }, at()) })
    : () => pick(it, at());
  b.addEventListener('click', (e) => { e.stopPropagation(); run(e); });
};

// The submenu grows out of, and goes back into, its item's ▸ arrow (the item's centre without one).
const arrowPoint = (trigger) => {
  const a = trigger.querySelector?.('.project-menu-arrow');
  return a ? rectCenter(a) : rectCenter(trigger);
};

// `pick(child, rect)` runs a nested item; the parent closes everything first. `siblings()` is its
// level's lists: showing this one folds the others, so one flyout is out per level.
export const createRowSubmenu = (trigger, items, pick, siblings = () => []) => {
  let sub = null;
  let nested = [];
  const hideNested = () => { for (const n of nested) n.hide(); };
  const hide = () => {
    if (!sub) return;
    hideNested();
    nested = [];
    popupOut(sub, arrowPoint(trigger), SURFACE_MENU_OUT_MS);
    sub.remove();
    sub = null;
    trigger.classList.remove('is-open');
  };
  const place = () => {
    const r = trigger.getBoundingClientRect();
    const w = sub.offsetWidth;
    const h = sub.offsetHeight;
    const x = r.right + GAP + w > window.innerWidth - EDGE ? r.left - GAP - w : r.right + GAP;
    const y = Math.min(r.top - 5, window.innerHeight - EDGE - h);
    sub.style.left = `${Math.max(EDGE, x)}px`;
    sub.style.top = `${Math.max(EDGE, y)}px`;
  };
  const show = () => {
    if (sub) return;
    for (const s of siblings()) if (s !== self) s.hide();
    sub = document.createElement('div');
    sub.className = 'project-menu project-submenu';
    for (const it of items) {
      if (!it) continue;
      const b = menuItemButton(it);
      if (it.items) nested.push(createRowSubmenu(b, it.items, pick, () => nested));
      else {
        b.addEventListener('mouseenter', hideNested);
        wireActionItem(b, it, pick);
      }
      b.addEventListener('keydown', (e) => {
        if (e.key !== 'ArrowLeft' && e.key !== 'Escape') return;
        e.stopPropagation();
        hide();
        trigger.focus();
      });
      sub.appendChild(b);
    }
    document.body.appendChild(sub);
    place();
    trigger.classList.add('is-open');
    popupIn(sub, arrowPoint(trigger), SURFACE_MENU_IN_MS);
  };
  // A click only opens: the pointer that hovered the list out must not click it shut again.
  trigger.addEventListener('click', (e) => { e.stopPropagation(); show(); });
  trigger.addEventListener('mouseenter', show);
  trigger.addEventListener('keydown', (e) => {
    if (e.key !== 'ArrowRight') return;
    e.preventDefault();
    show();
    sub.querySelector('button')?.focus();
  });
  const self = { hide, contains: (t) => !!sub && (sub.contains(t) || nested.some((n) => n.contains(t))) };
  return self;
};
