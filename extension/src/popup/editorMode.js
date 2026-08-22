// ── Editor mode: the panel while it stands ON the Stencil editor ─────────────
// The two sections popup.js hands over on an editor tab — "Open editors" and "Images
// from another page" — plus the import path INTO this editor tab (asking first when it
// already holds an image). Decisions are pure in lib/editorTabs.js; nothing here writes
// the editor's registry. Tab titles, project and image names are page DATA → textContent.
import { MSG } from '../lib/messages.js';
import { matchEditors, matchSourceTabs } from '../lib/editorTabs.js';
import { editableSrc, sourceOf } from '../lib/imageModel.js';
import { icon } from '../lib/icons.js';
import { createFilterTransition } from '../lib/motion.js';
import { openPanelDialog } from './dialogShell.js';

// Preview refresh interval (ms) — the same poll-while-open the shared pins use, an MV3 popup
// being too short-lived for a background channel. Cleared on pagehide / leaving editor mode.
const EDITOR_POLL_MS = 8000;
// Debounce for tab open/close/navigate bursts before the source-page list refreshes.
const TABS_REFRESH_DEBOUNCE_MS = 300;

// Long edge (px) of the hover magnifier's re-capture — big enough to read a drawing on,
// small enough to cross three message hops without a stall.
const MAGNIFY_PX = 1024;

// One request/response round-trip to the service worker, normalising a missing receiver
// (sleeping worker, reloading extension) to the same `{ok:false,error}` every handler uses.
const ask = async (message) => {
  try {
    return (await chrome.runtime.sendMessage(message)) || { ok: false, error: 'no receiver' };
  } catch (err) {
    return { ok: false, error: err.message || 'no receiver' };
  }
};

// Build the editor surface from popup.js's injected pieces (setStatus, run, dismiss, the
// SHARED ⋯ menu, onSourceTab, imageDataUrl, the floating preview) so this holds none of its state.
// Returns { available, isLiveEditor, setEditorTab, sourceTab, importHere, refresh }.
export const createEditorMode = ({ setStatus, run, dismiss, menu, onSourceTab, imageDataUrl, preview }) => {
  const listEl = document.getElementById('ed-list');
  const searchEl = document.getElementById('ed-search');
  const regexEl = document.getElementById('ed-regex');
  const refreshBtn = document.getElementById('ed-refresh');
  const listedEl = document.getElementById('src-list');
  const srcFilterEl = document.getElementById('src-filter');
  const srcRegexEl = document.getElementById('src-regex');
  const allBtn = document.getElementById('src-all');
  const noneBtn = document.getElementById('src-none');
  const rescanBtn = document.getElementById('src-rescan');
  const noteEl = document.getElementById('src-note');
  // The DevTools panel has no editor sections (it is pinned to one tab), so every method
  // no-ops there and popup.js keeps the classic surface.
  const present = !!(listEl && listedEl);
  // Both lists are rebuilt wholesale on every search/filter keystroke — the transition
  // (lib/motion.js) fades the rows the filter dropped out where they stood and ramps the
  // new ones in. Rows are keyed by tabId, so re-rendering the same tabs animates nothing.
  const edTransition = createFilterTransition({ list: listEl });
  const srcTransition = createFilterTransition({ list: listedEl });

  let editorTabId = null;   // the editor tab this panel stands on (null = page mode)
  let editors = [];         // the last EDITOR_LIST rows (editorRow shape)
  let choices = [];         // the last SOURCE_TABS choices
  const selected = new Set();   // tabIds ticked in the source list (empty = list nothing)
  let pollTimer = null;
  const magnified = new Map();   // tabId → the big hover capture, fetched once per tab

  // ── "Open editors" ─────────────────────────────────────────────────────────
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
    if (title) b.title = title;
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
    el.title = [row.projectName || row.title || row.url, row.current ? '(the tab this panel is on)' : '',
      row.url, 'Click: focus this editor tab'].filter(Boolean).join('\n');

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
    more.title = 'Actions';
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
    if (editorTabId == null) return;
    const res = await ask({ type: MSG.EDITOR_LIST, tabId: editorTabId, thumbnails: true });
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

  // ── "Images from another page" ─────────────────────────────────────────────
  // A MULTI-select list, not a one-of dropdown: comparing tabs is the ordinary case, and
  // the list below merges whatever is ticked. Nothing is ticked to begin with — the list
  // stays empty until you choose, rather than guessing at a page for you.
  // Ticked pages, in the order they are listed. popup.js scans exactly these.
  const pickedChoices = () => choices.filter((c) => selected.has(c.tabId));

  // The choices the URL filter currently admits (regex when the pill is on).
  const visibleChoices = () => matchSourceTabs(choices, srcFilterEl ? srcFilterEl.value : '',
    { regex: !!(srcRegexEl && srcRegexEl.checked) });

  const setPicked = (tabId, on) => {
    if (on) selected.add(tabId); else selected.delete(tabId);
    renderChoices();
    onSourceTab(pickedChoices());
  };

  // ⋯ per page: tick/untick (the same thing the row click does, named), go look at that tab,
  // and copy its URL — the three things you want from a list of pages you are not on.
  const choiceMenuNodes = (c) => [
    menuItem(icon(selected.has(c.tabId) ? 'x' : 'check', { size: 15 }),
      selected.has(c.tabId) ? 'Deselect this page' : 'Select this page',
      () => setPicked(c.tabId, !selected.has(c.tabId))),
    menuItem(icon('monitor', { size: 15 }), 'Focus this page', async () => {
      const res = await ask({ type: MSG.EDITOR_FOCUS_TAB, tabId: c.tabId });
      if (!res.ok) { setStatus(`Couldn’t focus that page (${res.error}).`); return; }
      dismiss();
    }),
    menuItem(icon('external', { size: 15 }), 'Copy page URL', async () => {
      try {
        await navigator.clipboard.writeText(c.url);
        setStatus('Page URL copied.');
      } catch {
        setStatus('Couldn’t copy the URL (clipboard unavailable).');
      }
    }),
  ];

  const renderChoiceRow = (c) => {
    const li = document.createElement('li');
    li.dataset.key = String(c.tabId);   // the filter transition diffs renders by this
    const el = document.createElement('div');
    el.className = 'src-row' + (selected.has(c.tabId) ? ' picked' : '');
    el.title = `${c.title || c.host}\n${c.url}\n\nClick: ${selected.has(c.tabId) ? 'stop listing' : 'list'} this page’s images`;

    const box = document.createElement('input');
    box.type = 'checkbox';
    box.checked = selected.has(c.tabId);
    box.addEventListener('click', (e) => {
      e.stopPropagation();                       // the row's own click would toggle it back
      setPicked(c.tabId, box.checked);
    });

    // The tab's own favicon, so a list of same-site tabs is still tellable apart at a glance.
    // It is a page-controlled URL, so it only ever becomes an <img src> — never markup — and
    // a broken one collapses to the neutral slot rather than an icon-shaped hole.
    const fav = document.createElement('img');
    fav.className = 'src-fav';
    fav.alt = '';
    if (c.favIconUrl) fav.src = c.favIconUrl;
    fav.addEventListener('error', () => { fav.style.visibility = 'hidden'; });

    const meta = document.createElement('div');
    meta.className = 'meta';
    const name = document.createElement('div');
    name.className = 'name';
    name.textContent = c.title || c.host || c.url;      // page data → text
    const host = document.createElement('div');
    host.className = 'sub';
    const dim = document.createElement('span');
    dim.className = 'dim';
    dim.textContent = c.host;
    host.appendChild(dim);
    meta.append(name, host);

    const more = document.createElement('button');
    more.className = 'more-btn';
    more.textContent = '⋯';
    more.title = 'Actions';
    more.addEventListener('click', (e) => {
      e.stopPropagation();
      menu.open(more, choiceMenuNodes(c));
    });

    // Clicking the row IS "add this page" (and clicking a ticked one drops it again) — the
    // default action, so the common case never costs a trip through the ⋯ menu.
    el.addEventListener('click', () => setPicked(c.tabId, !selected.has(c.tabId)));
    el.append(box, fav, meta, more);
    li.appendChild(el);
    return li;
  };

  const renderChoices = () => {
    // Drop ticks for tabs that have since closed, so the count can't outrun the list.
    for (const id of [...selected]) if (!choices.some((c) => c.tabId === id)) selected.delete(id);
    srcTransition.begin();
    listedEl.textContent = '';
    const shown = visibleChoices();
    if (!choices.length) {
      const li = document.createElement('li');
      li.className = 'ed-empty';
      li.textContent = 'No other pages are open.';
      listedEl.appendChild(li);
    } else if (!shown.length) {
      const li = document.createElement('li');
      li.className = 'ed-empty';
      li.textContent = 'No open page matches that filter.';
      listedEl.appendChild(li);
    } else {
      for (const c of shown) listedEl.appendChild(renderChoiceRow(c));
    }
    srcTransition.end();
    const n = selected.size;
    noteEl.textContent = n
      ? `Listing the images on ${n} page${n === 1 ? '' : 's'} — open one into this editor.`
      : (choices.length ? 'Tick a page to list its images here.' : '');
    if (allBtn) allBtn.disabled = !shown.length || shown.every((c) => selected.has(c.tabId));
    if (noneBtn) noneBtn.disabled = !n;
  };

  // The pages worth offering (lib/editorTabs.js `sourceTabChoices`, applied in the worker):
  // every open http(s) tab that isn't an editor and isn't a blocked scheme.
  const refreshChoices = async () => {
    const res = await ask({ type: MSG.SOURCE_TABS });
    choices = res.ok ? (res.tabs || []) : [];
    if (!res.ok) setStatus(`Couldn’t list the open pages (${res.error}).`);
    renderChoices();
  };

  // A yes/no dialog on the shared shell (dialogShell.js): click-away, Escape and Cancel
  // all mean no, so a destructive action needs a deliberate click on its own button.
  // With an `anchor` it opens as the popover pinned next to the asking control.
  const confirmDialog = (titleText, subject, warning, confirmLabel, anchor) =>
    openPanelDialog({
      anchor,
      build: (finish) => {
        const title = document.createElement('div');
        title.className = 'dialog-title';
        title.textContent = titleText;
        const what = document.createElement('div');
        what.className = 'dialog-note';
        what.textContent = [subject, warning].filter(Boolean).join(' — ');   // page data → text

        const row = document.createElement('div');
        row.className = 'dialog-actions';
        const cancel = document.createElement('button');
        cancel.textContent = 'Cancel';
        cancel.addEventListener('click', () => finish(false));
        const ok = document.createElement('button');
        ok.className = 'primary';
        ok.textContent = confirmLabel;
        ok.addEventListener('click', () => finish(true));
        row.append(cancel, ok);
        return [title, what, row];
      },
    }).then((v) => v === true);   // undefined (click-away / Escape) reads as "no"

  // ── Import into the editor tab this panel stands on ────────────────────────
  // The occupied-editor chooser, on the shared shell. Resolves the chosen ImportMode, or
  // undefined when cancelled (click-away / Escape). With an `anchor` it opens as a
  // popover next to the clicked row, so the image being imported stays in view.
  const promptImportMode = (state, anchor) => openPanelDialog({
    anchor,
    build: (finish) => {
      const title = document.createElement('div');
      title.className = 'dialog-title';
      title.textContent = 'This editor already holds an image.';
      const what = document.createElement('div');
      what.className = 'dialog-note';
      // The project + image names come from the editor page → text, never markup.
      what.textContent = [state.projectName || '(unnamed project)', state.imageName].filter(Boolean).join(' · ')
        + (state.incognito ? ' · not saved (incognito)' : '');

      const row = document.createElement('div');
      row.className = 'dialog-actions';
      const choice = (label, val, primary = false) => {
        const b = document.createElement('button');
        if (primary) b.className = 'primary';
        b.textContent = label;
        b.addEventListener('click', () => finish(val));
        return b;
      };
      row.append(
        choice('Add as a new project', 'new', true),
        choice('Replace the image', 'replace'),
        choice('Replace, keep the annotations', 'replace-keep'),
        choice('Cancel', undefined),
      );
      return [title, what, row];
    },
  });

  // The descriptor names bytes and provenance separately: `src` is the image to load, `source`
  // what the hand-off records (a video row's frame vs its media URL). `src` is resolved HERE
  // via imageDataUrl — an SVG must be rasterised first and only a document can draw one.
  const importDescriptor = (image, dataUrl) => ({
    name: image.name,
    kind: 'img',
    src: dataUrl,
    source: sourceOf(image) || editableSrc(image),
  });

  const importOnce = (image, { dataUrl, incognito, crop, page, mode }) => ask({
    type: MSG.EDITOR_IMPORT,
    tabId: editorTabId,
    image: importDescriptor(image, dataUrl),
    resource: image.resource || '',      // the row remembers which page it came from
    incognito: !!incognito,
    // Crop rect + page key ride along untouched; only the assistant's `open` op sets them.
    ...(crop ? { crop } : {}),
    ...(page ? { page } : {}),
    mode,
  });

  // Open one scanned row INTO the editor tab this panel stands on. Sent `mode:'ask'`: a
  // blank editor imports at once, an occupied one returns `needsChoice` for the chooser
  // above (pinned to `anchor` when given). Resolves true once the image landed; false
  // when nothing was imported — what lets the assistant fall back to the new-tab hand-off.
  const importHere = async (image, { incognito = false, crop = null, page = '', anchor = null } = {}) => {
    if (!present || editorTabId == null) return false;
    // A shared (server) row's bytes sit behind Bearer auth the worker's fetch can't present —
    // those open through the ⋯ menu's "In editor", which resolves them here instead.
    if (image.shared) {
      setStatus('A server-stored image opens in its own editor tab — use ⋯ → Open ▸ In editor.');
      return false;
    }
    if (!editableSrc(image)) {
      setStatus(`“${image.name}” has no image to open — a video needs a captured frame first.`);
      return false;
    }
    setStatus('Loading image…');
    // Resolved once, then reused for the retry below: the chooser round-trip must not
    // re-fetch (and re-rasterise) the same image.
    let dataUrl;
    try {
      dataUrl = await imageDataUrl(image);
    } catch (err) {
      setStatus(`Import failed: ${err.message}`);
      return false;
    }
    setStatus('Importing into this editor…');
    let res = await importOnce(image, { dataUrl, incognito, crop, page, mode: 'ask' });
    if (!res.ok && res.needsChoice) {
      const mode = await promptImportMode(res.state || {}, anchor);
      if (!mode) {
        setStatus('');
        return false;
      }
      res = await importOnce(image, { dataUrl, incognito, crop, page, mode });
    }
    if (!res.ok) {
      setStatus(`Import failed: ${res.error}`);
      return false;
    }
    setStatus(`Imported into “${res.projectName || 'this editor'}”.`);
    await refreshEditors();
    dismiss();
    return true;
  };

  // ── Live source-page list ───────────────────────────────────────────────────
  // The choices follow the browser: opening, closing, or navigating a tab
  // refreshes the list (debounced — a burst of tab events is one refresh). A
  // TICKED tab that went away also drops its images from the merged list.
  let tabsRefreshTimer = null;
  const refreshChoicesLive = () => {
    if (editorTabId == null) return;   // the section only exists in editor mode
    if (tabsRefreshTimer) clearTimeout(tabsRefreshTimer);
    tabsRefreshTimer = setTimeout(() => {
      tabsRefreshTimer = null;
      run(async () => {
        const before = pickedChoices().map((c) => c.tabId).join(',');
        await refreshChoices();
        const picked = pickedChoices();   // renderChoices pruned dead tabIds
        if (picked.map((c) => c.tabId).join(',') !== before) onSourceTab(picked);
      });
    }, TABS_REFRESH_DEBOUNCE_MS);
  };

  // ── Poll-while-open (previews) ─────────────────────────────────────────────
  const startPolling = () => {
    if (pollTimer) return;
    pollTimer = setInterval(() => {
      refreshEditors();
      // The DevTools panel has no chrome.tabs events — its page list rides the
      // same poll instead (popup/side panel refresh on the events below).
      if (!chrome.tabs?.onCreated) refreshChoicesLive();
    }, EDITOR_POLL_MS);
  };
  const stopPolling = () => {
    if (pollTimer) clearInterval(pollTimer);
    pollTimer = null;
  };

  if (present) {
    window.addEventListener('pagehide', stopPolling);
    // Keep the source-page list in step with the browser's tabs. chrome.tabs
    // events exist in the popup and side panel; the DevTools panel lacks the
    // API entirely (optional chaining) and refreshes via the poll instead.
    chrome.tabs?.onCreated?.addListener(refreshChoicesLive);
    chrome.tabs?.onRemoved?.addListener(refreshChoicesLive);
    chrome.tabs?.onUpdated?.addListener((tabId, info) => {
      // Only changes the list can SHOW: a navigation, a retitle, or a load
      // settling — not every favicon/audible/status flicker.
      if (info.url || info.title || info.status === 'complete') refreshChoicesLive();
    });
    searchEl.addEventListener('input', renderEditors);
    regexEl.addEventListener('change', renderEditors);
    refreshBtn.addEventListener('click', () => run(refreshEditors));
    rescanBtn.addEventListener('click', () => run(async () => {
      await refreshChoices();
      onSourceTab(pickedChoices());   // re-scan whatever is still ticked (nothing = clear the list)
    }));
    srcFilterEl?.addEventListener('input', renderChoices);
    srcRegexEl?.addEventListener('change', renderChoices);
    // Select all applies to what the FILTER shows, so "regex + select all" is one gesture.
    allBtn?.addEventListener('click', () => {
      for (const c of visibleChoices()) selected.add(c.tabId);
      renderChoices();
      onSourceTab(pickedChoices());
    });
    noneBtn?.addEventListener('click', () => {
      selected.clear();
      renderChoices();
      onSourceTab(pickedChoices());
    });
  }

  return {
    // False on a surface without the editor-mode markup (the DevTools panel), so popup.js
    // never flips into a mode this document can't render.
    available: present,

    // Is this tab REALLY a Stencil editor — does its bridge answer? Origin matching alone
    // also matches ordinary pages served beside the editor (popup.js resolveScanTab).
    async isLiveEditor(tabId) {
      if (!present || tabId == null) return false;
      const res = await ask({ type: MSG.EDITOR_STATE, tabId, thumbnail: false });
      return !!(res && res.ok);
    },

    // Enter (or leave) editor mode. Called from every scan, so it stays synchronous:
    // the refreshes it starts are deliberately not awaited.
    setEditorTab(tabId) {
      if (!present) return;
      editorTabId = tabId == null ? null : tabId;
      if (editorTabId == null) {
        stopPolling();
        editors = [];
        listEl.textContent = '';
        return;
      }
      run(refreshChoices);
      run(refreshEditors);
      startPolling();
    },

    // The pages whose images the ordinary list shows — [] while none is ticked. popup.js
    // scans each and merges the results, tagging every row with the tab it came from.
    sourceTabs() {
      return pickedChoices();
    },

    importHere,

    // Re-pull both lists (the source page went away mid-scan, or a manual refresh).
    refresh() {
      if (!present || editorTabId == null) return;
      run(refreshChoices);
      run(refreshEditors);
    },
  };
};
