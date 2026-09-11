// ── "Open editors": one row per open Stencil editor tab ─────────────────────
// The section's own state (the last EDITOR_LIST rows, the hover captures) and the
// row menu. Every capability is injected — nothing here writes the editor's registry.
import { MSG } from '../lib/messages.js';
import { matchEditors } from '../lib/editorTabs.js';
import { icon } from '../lib/icons.js';
import { createFilterTransition } from '../lib/motion.js';
import { setTip } from '../lib/tip.js';
import { confirmDialog } from './editorDialogs.js';

// Long edge (px) of the hover magnifier's re-capture — big enough to read a drawing on,
// small enough to cross three message hops without a stall.
const MAGNIFY_PX = 1024;

export const createEditorList = ({ listEl, searchEl, regexEl, ask, menu, setStatus,
                                   dismiss, run, preview, getEditorTabId }) => {
  // Rebuilt wholesale on every search keystroke — the transition (lib/motion.js) fades
  // the rows the search dropped out where they stood and ramps the new ones in.
  const edTransition = createFilterTransition({ list: listEl });
  let editors = [];              // the last EDITOR_LIST rows (editorRow shape)
  const magnified = new Map();   // tabId → the big hover capture, fetched once per tab

  const emptyRow = (text) => {
    const li = document.createElement('li');
    li.className = 'ed-empty';
    li.textContent = text;
    listEl.appendChild(li);
  };

  // A small outline badge, matching the image rows' `.badge` pills.
  const badge = (cls, text, title) => {
    const b = document.createElement('span');
    b.className = `badge ${cls}`;
    b.textContent = text;
    if (title) setTip(b, title);
    return b;
  };

  // One menu row — popup.js's shared `item()`, whose label is a TEXT NODE (a project
  // name comes from the editor page, so it must never reach innerHTML).
  const menuItem = menu.item;

  // Focus an editor tab (and raise its window) — the row's click action and its first
  // menu item. The popup closes afterwards, so the tab it just raised is what you see.
  const focusEditor = async (row) => {
    const res = await ask({ type: MSG.EDITOR_FOCUS_TAB, tabId: row.tabId });
    if (!res.ok) {
      setStatus(`Couldn’t focus that editor (${res.error}).`);
      return;
    }
    dismiss();
  };

  // Switch one editor tab to another of ITS OWN projects, in place — the editor page runs
  // its `switchToProject`, so nothing navigates and nothing in that tab is lost.
  const switchProject = async (row, project) => {
    const res = await ask({ type: MSG.EDITOR_SWITCH_PROJECT, tabId: row.tabId, projectId: project.id });
    if (!res.ok) {
      setStatus(`Couldn’t switch project (${res.error}).`);
      return;
    }
    setStatus(`Switched that editor to “${res.projectName || project.name}”.`);
    await refreshEditors();
  };

  // Close an editor tab. Confirmed first: the tab may hold an unsaved incognito session, and
  // even a saved project loses its zoom/selection — a menu click must not discard that silently.
  const closeEditor = async (row, anchor) => {
    const label = row.projectName || row.title || row.url;
    const ok = await confirmDialog('Close this editor tab?', label,
      row.incognito ? 'This is an incognito editor — its project is never saved, so its work is lost.' : '',
      'Close', anchor);
    if (!ok) return;
    try { await chrome.tabs.remove(row.tabId); } catch { /* already gone */ }
    await refreshEditors();
  };

  // Focus / Switch project ▸ (that tab's projects, active one ticked) / Close. No "open in a
  // new tab": the row IS an open tab, so the only useful destinations are it and the bin.
  // `anchor` = the ⋯ button the menu opened from; the close-confirm popover pins to it.
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
    // The accent outline alone says "this tab" (see .ed-row.current) — a badge saying it too
    // cost a whole line in a 400px panel. The full name lives here, since the row ellipsizes it.
    setTip(el, [row.projectName || row.title || row.url, row.current ? '(the tab this panel is on)' : '',
      row.url, 'Click: focus this editor tab'].filter(Boolean).join('\n'));

    // Live preview (the tab's #canvas, downscaled by the editor page, unsaved edits included).
    // A silent bridge sends none — leave the slot blank, never an empty src.
    const thumb = document.createElement('img');
    thumb.className = 'ed-thumb' + (row.thumbnail ? '' : ' blank');
    thumb.alt = '';
    if (row.thumbnail) thumb.src = row.thumbnail;
    // Hovering magnifies it, like the image rows' thumbnails. The row preview is a 256px
    // JPEG — readable as a 40px chip, mush when blown up — so the hover asks that tab to
    // re-capture at MAGNIFY_PX. Cached per tab, and the small one shows meanwhile.
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
    // Project name, falling back to the tab's own title/URL for an editor that hasn't
    // named (or hasn't reported) one. All three are page data → textContent.
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

  // Render the fetched rows through the search box — `matchEditors`, i.e. exactly the image
  // list's rules (empty matches all, substring by default, the pill switches to regex).
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

  // Re-pull every open editor tab + the state its bridge reports (previews included).
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
    magnified.clear();   // those captures are of a canvas that has since moved on
    renderEditors();
  };

  return {
    render: renderEditors,
    refresh: refreshEditors,
    clear() { editors = []; listEl.textContent = ''; },
  };
};
