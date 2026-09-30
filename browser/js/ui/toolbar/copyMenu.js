// The Image section's "Make a copy" button: a list of the three scopes, each opening the copy
// confirmation (ui/modal/copyProjectModal.js) grown out of its row. A click toggles the list at
// once; right-click, long press and Alt+hover open it as every modal icon's popover does.
import { showMenu, hideMenu } from '../control/dropdownMenu.js';
import { wireModalOpenGestures } from '../tip/popover.js';
import { icon } from '../icons.js';
import { COPY_SCOPES, COPY_SCOPE_LABELS, COPY_SCOPE_ICONS } from '../../core/project/copy/options.js';

const scopeRow = (what, pick) => {
  const li = document.createElement('li');
  li.className = 'accent-dd-opt';
  li.setAttribute('role', 'option');
  li.dataset.copyScope = what;
  li.innerHTML = `<span class="ctx-icon">${icon(COPY_SCOPE_ICONS[what])}</span><span class="ctx-label">${COPY_SCOPE_LABELS[what]}</span>`;
  li.addEventListener('click', () => pick(what, li.getBoundingClientRect()));
  return li;
};

let home = null;
// The toolbar's copy button, where a copy started from the canvas menu flies home (null unwired).
export const copyProjectButton = () => home;

export function wireCopyProjectMenu(trigger, app) {
  if (!trigger) return;
  home = trigger;
  const menu = document.createElement('ul');
  menu.className = 'accent-dd-menu';
  menu.setAttribute('role', 'listbox');
  menu.hidden = true;
  trigger.insertAdjacentElement('afterend', menu);

  const pick = (what, at) => {
    close();
    document.querySelector('stencil-copy-project-modal')?.openFor({ id: null, what }, { from: at, backTo: trigger });
  };
  for (const what of COPY_SCOPES) menu.appendChild(scopeRow(what, pick));

  const onDocDown = (e) => { if (!trigger.contains(e.target) && !menu.contains(e.target)) close(); };
  const onKey = (e) => { if (e.key === 'Escape') close(); };
  const openPopover = () => {
    if (!app.image || !menu.hidden) return;
    showMenu(menu, trigger);
    document.addEventListener('pointerdown', onDocDown, true);
    document.addEventListener('keydown', onKey);
  };
  const close = () => {
    if (menu.hidden) return;
    g.notifyClosed();
    document.removeEventListener('pointerdown', onDocDown, true);
    document.removeEventListener('keydown', onKey);
    hideMenu(menu);
  };
  const g = wireModalOpenGestures(trigger, {
    openFull: () => (menu.hidden ? openPopover() : close()),
    openPopover,
    closePopover: close,
    isPopoverOpen: () => !menu.hidden,
    isPeekEngaged: () => menu.matches(':hover'),
    eagerClick: true,
  });
  menu.addEventListener('mouseenter', () => g.boxEnter());
  menu.addEventListener('mouseleave', () => g.boxLeave());
}
