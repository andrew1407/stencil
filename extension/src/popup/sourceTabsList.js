// ── "Images from another page": the open pages whose images the list merges ──
// A MULTI-select list, not a one-of dropdown: comparing tabs is the ordinary case, and
// the list below merges whatever is ticked. Nothing is ticked to begin with — the list
// stays empty until you choose, rather than guessing at a page for you.
import { MSG } from '../lib/messages.js';
import { matchSourceTabs } from '../lib/editorTabs.js';
import { icon } from '../lib/icons.js';
import { createFilterTransition } from '../lib/motion.js';
import { setTip } from '../lib/tip.js';

export const createSourceTabs = ({ listedEl, srcFilterEl, srcRegexEl, allBtn, noneBtn,
                                   noteEl, ask, menu, setStatus, dismiss, onSourceTab }) => {
  const srcTransition = createFilterTransition({ list: listedEl });
  let choices = [];             // the last SOURCE_TABS choices
  const selected = new Set();   // tabIds ticked in the list (empty = list nothing)
  const menuItem = menu.item;

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
    setTip(el, `${c.title || c.host}\n${c.url}\n\nClick: ${selected.has(c.tabId) ? 'stop listing' : 'list'} this page’s images`);

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
    setTip(more, 'Actions', { label: true });
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

  return {
    render: renderChoices,
    refresh: refreshChoices,
    picked: pickedChoices,
    selectAll() { for (const c of visibleChoices()) selected.add(c.tabId); renderChoices(); },
    clearSelection() { selected.clear(); renderChoices(); },
  };
};
