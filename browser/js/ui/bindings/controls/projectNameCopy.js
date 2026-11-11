// The top bar's copy button beside the project colour: a click copies the open project's name; a
// right-click opens its menu — the name, then each link it has — where a click copies and a
// double click shares. `root` holds the button: the document, or a test's fragment.
import { createProjectRowMenu } from '../../projects/window/projectRowMenu.js';
import { liveFacts, nameItem, linkItems } from '../../share/projectMenu.js';

const COPY_NAME_ID = 'project-name-copy-btn';

export function wireProjectNameCopy(app, root) {
  const btn = root?.querySelector?.(`#${COPY_NAME_ID}`);
  if (!btn) return;
  const menu = createProjectRowMenu();
  const items = () => {
    const facts = liveFacts(app);
    return [nameItem(facts, true), ...linkItems(facts)];
  };
  btn.addEventListener('click', (e) => { e.stopPropagation(); nameItem(liveFacts(app))?.onClick(); });
  btn.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    e.stopPropagation();
    menu.showMenu(btn, items(), { x: e.clientX, y: e.clientY }, { from: { x: e.clientX, y: e.clientY } });
  });
}
