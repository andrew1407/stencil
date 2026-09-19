// The projects list's session preferences: sort mode, the manual drag order and the search
// scope. sessionStorage only — never the shared localStorage registry with its C++ core twin.
import { rowMatches } from '../base.js';
import { SORT_MODES } from '../projectSort.js';

const SORT_KEY = 'stencil_projects_sortmode';
const ORDER_KEY = 'stencil_projects_order';
const SEARCH_MODE_KEY = 'stencil_projects_searchmode';
const SEARCH_MODES = ['common', 'names', 'keywords'];
const ssGet = (k) => { try { return window.sessionStorage.getItem(k); } catch { return null; } };
const ssSet = (k, v) => { try { window.sessionStorage.setItem(k, v); } catch { /* private mode / disabled */ } };

export function createListPrefs({ sortEl, searchModeEl }) {
  const loadSortMode = () => { const v = ssGet(SORT_KEY); return SORT_MODES.includes(v) ? v : 'name'; };
  const loadSearchMode = () => { const v = ssGet(SEARCH_MODE_KEY); return SEARCH_MODES.includes(v) ? v : 'common'; };
  let sortMode = loadSortMode();
  let searchMode = loadSearchMode();
  const matchRow = (name, keywords, q) => {
    if (!q.trim()) return true;
    const kw = (keywords || []).join(' ');
    if (searchMode === 'names') return rowMatches(name || '', q);
    if (searchMode === 'keywords') return rowMatches(kw, q);
    return rowMatches(name || '', q) || rowMatches(kw, q);
  };
  const loadOrder = () => { try { const a = JSON.parse(ssGet(ORDER_KEY) || '[]'); return Array.isArray(a) ? a : []; } catch { return []; } };
  const saveOrder = (a) => ssSet(ORDER_KEY, JSON.stringify(a));
  const setSortMode = (m) => { sortMode = m; ssSet(SORT_KEY, m); if (sortEl) sortEl.value = m; };
  const setSearchMode = (m) => { searchMode = m; ssSet(SEARCH_MODE_KEY, m); };
  // The controls carry the remembered choice from the first paint on.
  const syncControls = () => { if (sortEl) sortEl.value = sortMode; if (searchModeEl) searchModeEl.value = searchMode; };
  syncControls();
  return { sortMode: () => sortMode, setSortMode, setSearchMode, syncControls, matchRow, loadOrder, saveOrder };
}
