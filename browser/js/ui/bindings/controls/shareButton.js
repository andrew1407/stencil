// The toolbar's share button: a click shares the current picture; a right-click opens its menu —
// Image › Current / No lines / Original, then each link the open project has — where a click
// shares and a double click copies (ui/share/projectMenu.js).
import { createProjectRowMenu } from '../../projects/window/projectRowMenu.js';
import { shareMenuItems } from '../../share/projectMenu.js';

export function wireShareButton(app, btn) {
  if (!btn) return;
  const menu = createProjectRowMenu();
  btn.addEventListener('click', () => app.export.shareImage());
  btn.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    e.stopPropagation();
    const items = shareMenuItems(app);
    if (items.length) menu.showMenu(btn, items, { x: e.clientX, y: e.clientY }, { from: { x: e.clientX, y: e.clientY } });
  });
}
