// "Open editors": one row per open Stencil editor tab. Every capability is injected;
// nothing here writes the editor's registry.
import { MSG } from '../../lib/messages.js';
import { matchEditors } from '../../lib/menu/editorTabs.js';
import { icon } from '../../lib/icons.js';
import { createFilterTransition } from '../../lib/motion.js';
import { setTip } from '../../lib/tip/tip.js';
import { confirmDialog } from './editorDialogs.js';

// Long edge (px) of the hover magnifier's re-capture: readable, yet crosses three
// message hops without a stall.
const MAGNIFY_PX = 1024;

export const createEditorList = ({ listEl, searchEl, regexEl, ask, menu, setStatus,
                                   dismiss, run, preview, getEditorTabId }) => {
  const edTransition = createFilterTransition({ list: listEl });
  let editors = [];              // the last EDITOR_LIST rows
  const magnified = new Map();   // tabId → the big hover capture

  const emptyRow = (text) => {
    const li = document.createElement('li');
    li.className = 'ed-empty';
    li.textContent = text;
    listEl.appendChild(li);
  };

  const badge = (cls, text, title) => {
    const b = document.createElement('span');
    b.className = `badge ${cls}`;
    b.textContent = text;
    if (title) setTip(b, title);
    return b;
  };

  // The item's label is a TEXT NODE: a project name comes from the editor page.
  const menuItem = menu.item;

  const focusEditor = async (row) => {
    const res = await ask({ type: MSG.EDITOR_FOCUS_TAB, tabId: row.tabId });
    if (!res.ok) {
      setStatus(`Couldn’t focus that editor (${res.error}).`);
      return;
    }
    dismiss();
  };

  // In place: the editor page runs its own `switchToProject`, nothing navigates.
  const switchProject = async (row, project) => {
    const res = await ask({ type: MSG.EDITOR_SWITCH_PROJECT, tabId: row.tabId, projectId: project.id });
    if (!res.ok) {
      setStatus(`Couldn’t switch project (${res.error}).`);
      return;
    }
    setStatus(`Switched that editor to “${res.projectName || project.name}”.`);
    await refreshEditors();
  };

  // Confirmed first: the tab may hold an unsaved incognito session.
  const closeEditor = async (row, anchor) => {
    const label = row.projectName || row.title || row.url;
    const ok = await confirmDialog('Close this editor tab?', label,
      row.incognito ? 'This is an incognito editor — its project is never saved, so its work is lost.' : '',
      'Close', anchor);
    if (!ok) return;
    try { await chrome.tabs.remove(row.tabId); } catch { /* already gone */ }
    await refreshEditors();
  };

  // `anchor` is the ⋯ button the menu opened from; the close-confirm popover pins to it.
  const rowMenuNodes = (row, anchor) => {
    const nodes = [menuItem(icon('monitor', { size: 15 }), 'Focus this editor', () => focusEditor(row))];
    if (row.projects.length) {
      nodes.push(menu.submenu(icon('refresh', { size: 15 }), 'Switch project', row.projects.map((p) =>
        menuItem(p.active ? icon('check', { size: 15 }) : '', p.name || '(unnamed)', () => switchProject(row, p)))));
    }
    nodes.push(menuItem(icon('trash', { size: 15 }), 'Close this editor…', () => closeEditor(row, anchor)));
    return nodes;
  };

  const renderEditorRow = (row) => {
    const li = document.createElement('li');
    li.dataset.key = String(row.tabId);   // the filter transition diffs renders by this
    const el = document.createElement('div');
    el.className = 'ed-row' + (row.current ? ' current' : '');
    setTip(el, [row.projectName || row.title || row.url, row.current ? '(the tab this panel is on)' : '',
      row.url, 'Click: focus this editor tab'].filter(Boolean).join('\n'));

    // A silent bridge sends no thumbnail: leave the slot blank, never an empty src.
    const thumb = document.createElement('img');
    thumb.className = 'ed-thumb' + (row.thumbnail ? '' : ' blank');
    thumb.alt = '';
    if (row.thumbnail) thumb.src = row.thumbnail;
    // The row preview is a 256px JPEG, mush when blown up: the hover re-captures at MAGNIFY_PX.
    if (row.thumbnail && preview) {
      preview.bind(thumb, row.thumbnail, async () => {
        if (magnified.has(row.tabId)) return magnified.get(row.tabId);
        const res = await ask({ type: MSG.EDITOR_STATE, tabId: row.tabId, thumbMax: MAGNIFY_PX });
        const big = (res && res.ok && res.state && res.state.thumbnail) || '';
        if (big) magnified.set(row.tabId, big);
        return big;
      });
    }

    const meta = document.createElement('div');
    meta.className = 'meta';
    const name = document.createElement('div');
    name.className = 'name';
    // All three are page data: textContent.
    name.textContent = row.projectName || row.title || row.url;
    const sub = document.createElement('div');
    sub.className = 'sub';
    if (row.incognito) sub.appendChild(badge('incog', 'incognito', 'This project is not persisted by the editor'));
    if (!row.ready) sub.appendChild(badge('offline', 'no answer', 'This tab’s bridge didn’t answer — an older editor build, or a page still loading'));
    const detail = document.createElement('span');
    detail.className = 'dim';
    detail.textContent = row.hasImage
      ? [row.imageName, row.imageSize ? `${row.imageSize.w}×${row.imageSize.h}` : ''].filter(Boolean).join(' · ')
      : (row.ready ? 'no image' : '');
    sub.appendChild(detail);
    meta.append(name, sub);

    const more = document.createElement('button');
    more.className = 'more-btn';
    more.textContent = '⋯';
    setTip(more, 'Actions', { label: true });
    more.addEventListener('click', (e) => {
      e.stopPropagation();
      menu.open(more, rowMenuNodes(row, more));
    });

    el.addEventListener('click', () => run(() => focusEditor(row)));
    el.append(thumb, meta, more);
    li.appendChild(el);
    listEl.appendChild(li);
  };

  const renderEditors = () => {
    edTransition.begin();
    listEl.textContent = '';
    if (!editors.length) {
      emptyRow('No editor tabs are open.');
    } else {
      const rows = matchEditors(editors, searchEl.value.trim(), { regex: regexEl.checked });
      if (!rows.length) emptyRow('No editors match the search.');
      else rows.forEach(renderEditorRow);
    }
    edTransition.end();
  };

  const refreshEditors = async () => {
    if (getEditorTabId() == null) return;
    const res = await ask({ type: MSG.EDITOR_LIST, tabId: getEditorTabId(), thumbnails: true });
    if (!res.ok) {
      editors = [];
      edTransition.begin();
      listEl.textContent = '';
      emptyRow(`Couldn’t list the open editors (${res.error}).`);
      edTransition.end();
      return;
    }
    editors = res.editors || [];
    magnified.clear();
    renderEditors();
  };

  return {
    render: renderEditors,
    refresh: refreshEditors,
    clear() { editors = []; listEl.textContent = ''; },
  };
};
