import { icon } from '../icons.js';
import { notify, shortName } from '../../utils.js';
import { leaveThenRemove, rowLeaveDust, ITEM_DUST_MS } from '../motion.js';
import { createOpenGesture } from '../../core/projectOpenGesture.js';
import { beginRowRename } from './rowRename.js';

// Everything a SAVED project row can do: the ⋯ / right-click menu, its prompts, and the open
// gestures. Returns the gesture so the rename editor can cancel an open it already armed.
export function attachRowActions(deps) {
  const {
    row, name, meta, app, close, render, serverLinked, hasServers, isPeerOpen,
    pickServer, confirmOpen, scrollRowIntoView, openColorPicker,
    beginRemoval, retireKey, localKey, rowById, showMenu,
  } = deps;
  let rowGesture = null;
  const beginRename = () => beginRowRename({ meta, name, app, render, gesture: rowGesture });
  const isActive = meta.id === app.activeProjectId;
  // True while THIS project is open in a DIFFERENT tab — removing/moving it
  // would yank it out from under that tab, so both are blocked then.
  const openElsewhere = () => isPeerOpen(meta);

  const moveToServer = async () => {
    if (openElsewhere()) { notify('Open in another tab — close it there first', 'fail'); return; }
    const urls = app.connections.urls;
    let address = urls[0];
    if (urls.length > 1) {
      address = await app.choose(
        `Move "${shortName(meta.name || 'Untitled')}" to which server? It becomes a server-backed project.`,
        { title: 'Move to server', confirmLabel: 'Move', confirmIcon: 'upload', closeAnchor: menuBtn, options: urls.map(u => ({ value: u, label: u })) });
      if (!address) return;
    } else if (!(await app.confirm(
      `Move "${shortName(meta.name || 'Untitled')}" to server ${address}? It becomes a server-backed project.`,
      { title: 'Move to server', confirmLabel: 'Move', confirmIcon: 'upload', closeAnchor: menuBtn }))) {
      return;
    }
    try { await app.moveProjectToServer(meta.id, address); notify('Moved to server', 'ok'); render(); scrollRowIntoView(meta.id); }
    catch (err) { notify(`Could not move to server — ${err.message}`, 'fail'); }
  };
  const copyToServer = async () => {
    const address = await pickServer(`Copy "${shortName(meta.name || 'Untitled')}" to which server?`, menuBtn);
    if (!address) return;
    const name = await app.prompt('Name for the server copy:', { title: 'Copy to server', confirmLabel: 'Copy', confirmIcon: 'copy', defaultValue: `${meta.name || 'Untitled'}-copy`, closeAnchor: menuBtn });
    if (name == null) return;
    try { await app.copyProjectToServer(meta.id, address, { name }); notify('Copied to server', 'ok'); render(); }
    catch (err) { notify(`Could not copy to server — ${err.message}`, 'fail'); }
  };
  const removeRow = async () => {
    if (openElsewhere()) { notify('Open in another tab — close it there first', 'fail'); return; }
    const note = serverLinked
      ? `Remove the local copy of "${shortName(meta.name || 'Untitled')}"? It stays on the server ${meta.address}.`
      : `Remove project "${shortName(meta.name || 'Untitled')}"? This cannot be undone.`;
    if (!(await app.confirm(note, { title: 'Remove project', danger: true, confirmIcon: 'trash', closeAnchor: menuBtn }))) return;
    // The row collapses away first; render() then rebuilds the list without it.
    const settle = beginRemoval();
    const revive = retireKey(localKey(meta.id));
    await leaveThenRemove(rowById(meta.id), () => {}, rowLeaveDust(1, 0, ITEM_DUST_MS));
    app.removeProject(meta.id);
    await settle();
    revive();
  };

  const openWithIntent = async ({ confirm = true, target = 'here', closeAnchor = null } = {}) => {
    if (target === 'newtab') {
      if (confirm && !(await confirmOpen(meta.name, true, closeAnchor))) return;
      app.openProjectInNewTab(meta.id);   // the same path the ⋯ menu uses
      return;
    }
    if (isActive) { close(); return; }
    if (confirm && !(await confirmOpen(meta.name, false, closeAnchor))) return;
    app.switchToProject(meta.id);
    close();
  };
  const open = () => openWithIntent({ confirm: true, target: 'here', closeAnchor: menuBtn });

  // Per-row colour: the native picker paints the project name, and a "Clear colour"
  // item (only when one is set) resets it to the theme accent.
  const pickColor = () => openColorPicker(meta, menuBtn);
  const clearColor = () => { app.setProjectColor(meta.id, ''); meta.color = ''; render(); };

  // Edit the project's search keywords via a prompt (comma/space separated). The store
  // normalizes; a server-linked project also pushes them to the server.
  const editKeywords = async () => {
    const cur = (meta.keywords || []).join(' ');
    const v = await app.prompt('Keywords (comma or space separated):', { title: 'Project keywords', titleIcon: 'info', confirmLabel: 'Save', confirmIcon: 'save', defaultValue: cur, multiline: true, closeAnchor: menuBtn });
    if (v == null) return;
    const updated = app.setProjectKeywords(meta.id, v.split(/[\s,]+/));
    if (updated) meta.keywords = updated.keywords;
    render();
  };

  // Edit the project's free-text description via a prompt. The store trims + stores it;
  // an empty value clears it. Mirrors editKeywords / the colour picker above.
  const editDescription = async () => {
    const cur = meta.description || '';
    const v = await app.prompt('Description:', { title: 'Project description', titleIcon: 'info', confirmLabel: 'Save', confirmIcon: 'save', defaultValue: cur, multiline: true, closeAnchor: menuBtn });
    if (v == null) return;
    const updated = app.setProjectDescription(meta.id, v);
    if (updated) meta.description = updated.description;
    render();
  };

  // One menu definition, shared by the "⋯" button and a right-click on the row.
  const menuItems = () => [
    isActive ? null : { icon: 'folder', label: 'Open', onClick: open },
    { icon: 'external', label: 'Open in new tab', onClick: async () => { if (await confirmOpen(meta.name, true, menuBtn)) app.openProjectInNewTab(meta.id); } },
    // Hidden when no target is configured, exactly as the toolbar button hides
    // (ui/controlState.js), so it never offers a dead action.
    app.openInAvailable?.() ? { icon: 'monitor', label: 'Open in another app', onClick: (at) => document.querySelector('stencil-open-in-modal')?.openFor(meta.id, { from: at, backTo: menuBtn }) } : null,
    { icon: 'pencil', label: 'Rename', onClick: () => beginRename() },
    { icon: 'palette', label: 'Set color', onClick: pickColor },
    meta.color ? { icon: 'x', label: 'Clear color', onClick: clearColor } : null,
    { icon: 'flag', label: 'Add keywords', onClick: editKeywords },
    { icon: 'file-text', label: 'Add description', onClick: editDescription },
    { icon: 'calendar', label: 'Set expiration', onClick: (at) => document.querySelector('stencil-expiration-modal')?.openFor(meta.id, { from: at, backTo: menuBtn }) },
    (hasServers() && !serverLinked) ? { icon: 'server', label: 'Move to server', onClick: moveToServer } : null,
    (hasServers() && !serverLinked) ? { icon: 'copy', label: 'Copy to server', onClick: copyToServer } : null,
    { icon: 'trash', label: 'Remove', danger: true, onClick: removeRow },
  ];

  const actions = document.createElement('div');
  actions.className = 'project-actions';
  const menuBtn = document.createElement('button');
  menuBtn.className = 'project-more btn-icon';
  menuBtn.dataset.title = 'More actions';
  menuBtn.innerHTML = icon('more', { size: 15 });
  menuBtn.addEventListener('click', e => {
    e.stopPropagation();
    showMenu(menuBtn, menuItems());
  });
  actions.appendChild(menuBtn);
  row.appendChild(actions);

  // Right-click (and, on touch, the long-press callout) opens the same overflow
  // menu at the cursor — that menu is where "Open in new tab" lives for fingers.
  row.addEventListener('contextmenu', e => {
    e.preventDefault();
    showMenu(menuBtn, menuItems(), { x: e.clientX, y: e.clientY });
  });

  row.classList.add('project-clickable');
  // ── Open gestures (see rowOpenIntent): click / dblclick / ⌘-variants on a
  // mouse, tap / long press on touch, Enter or Space from the keyboard. ──
  const gesture = createOpenGesture({ run: (intent) => openWithIntent(intent) });
  rowGesture = gesture;      // the rename editor cancels any pending open
  row._openGesture = gesture;   // …and so do BOTH drag engines (see attachRowDrag)
  row.addEventListener('click', (e) => gesture.click(e));
  row.addEventListener('dblclick', (e) => gesture.dblclick(e));
  // MOVEMENT WINS (see pressMove): past the slop the pending open is dropped and
  // the drop's click swallowed — the hold belongs to touchDrag's reorder pickup.
  row.addEventListener('pointerdown', (e) => {
    if (e.target.closest('input,button,select,.project-name-edit')) return;
    gesture.pressStart({ x: e.clientX, y: e.clientY });
  });
  row.addEventListener('pointermove', (e) => gesture.pressMove({ x: e.clientX, y: e.clientY }));
  for (const type of ['pointerup', 'pointercancel', 'pointerleave']) {
    row.addEventListener(type, () => gesture.pressEnd());
  }
  // A real drag pickup (mouse HTML5 DnD; the touch engine reports its own below)
  // kills any pending open outright.
  row.addEventListener('dragstart', () => gesture.dragStart());
  // Keyboard: the row is a real button. Enter/Space = a plain click (confirmed),
  // ⌘/Ctrl held targets a new tab — the same mapping as the mouse.
  row.tabIndex = 0;
  row.setAttribute('role', 'button');
  row.addEventListener('keydown', (e) => {
    if (e.key !== 'Enter' && e.key !== ' ') return;
    e.preventDefault();
    gesture.key(e);
  });
  return { gesture: rowGesture, beginRename };
}

// The incognito session has no ⋯ menu, but it CAN be published to a server — it then
// becomes a normal server-backed project and leaves incognito.
export function attachIncognitoActions({ row, app, render }) {
  const saveToServer = async () => {
    const urls = app.connections.urls;
    let address = urls[0];
    if (urls.length > 1) {
      address = await app.choose('Save this incognito project to which server?',
        { title: 'Save to server', confirmLabel: 'Save', confirmIcon: 'upload', options: urls.map(u => ({ value: u, label: u })) });
      if (!address) return;
    }
    try { await app.publishIncognitoToServer(address); render(); }
    catch (err) { notify(`Could not save to server — ${err.message}`, 'fail'); }
  };
  const actions = document.createElement('div');
  actions.className = 'project-actions';
  const btn = document.createElement('button');
  btn.className = 'project-more btn-icon';
  btn.dataset.title = 'Save to server';
  btn.innerHTML = icon('server', { size: 15 });
  btn.addEventListener('click', e => { e.stopPropagation(); saveToServer(); });
  actions.appendChild(btn);
  row.appendChild(actions);
}
