// A row menu item's nested list (projectRowMenu.js): a second `.project-menu` flown out beside
// its item — right, or left when the window has no room — on hover, click or ArrowRight, back
// on ArrowLeft / Escape. It lives on <body> like its parent, so the parent asks it `contains`.
import { icon } from '../../icons.js';
import { surfaceIn, surfaceOut, rectCenter, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from '../../motion.js';

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

// `pick(child, rect)` runs a nested item; the parent closes everything first.
export const createRowSubmenu = (trigger, items, pick) => {
  let sub = null;
  const hide = () => {
    if (!sub) return;
    surfaceOut(sub, rectCenter(trigger), { ms: SURFACE_MENU_OUT_MS });
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
    sub = document.createElement('div');
    sub.className = 'project-menu project-submenu';
    for (const it of items) {
      const b = menuItemButton(it);
      b.addEventListener('click', (e) => { e.stopPropagation(); pick(it, b.getBoundingClientRect()); });
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
    surfaceIn(sub, rectCenter(trigger), { ms: SURFACE_MENU_IN_MS });
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
  return { hide, contains: (t) => !!sub && sub.contains(t) };
};
