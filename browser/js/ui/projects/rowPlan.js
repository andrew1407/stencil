// Every row the current filter, search and sort would list, in order — the one source of truth
// for both render() and the filter transition's key delta (motion.js createFilterAnimator).
import { rowMatches } from '../base.js';
import { sortProjectItems } from '../projectSort.js';
import { makeIncognitoPeerRow } from './placeholderRows.js';

// One flat, sortable item list: a stable key, a lowercased name + date for the
// comparators, an isRemote flag, and a build() returning the row element.
const localRowKey = (m) => `local:${m.id}`;
const remoteRowKey = (m) => `remote:${m.serverUrl}:${m.id}`;
const metaName = (m) => (m.name || '').toLowerCase();
const metaDate = (m) => m.updatedAt || m.createdAt || 0;

export function createRowPlan(ctx) {
  const { app, store, search, prefs, remotes, filterMode, isPeerOpen, isServerMeta,
    makeRow, makeRemoteRow, incognitoPeers } = ctx;
  const showsServer = () => filterMode() === 'all' || filterMode() === 'server';
  // "Open elsewhere" / "Not open elsewhere" are local-only scopes: a not-yet-claimed
  // remote-cache row has no peers relationship to filter on.
  const showsPeerOpen = () => filterMode() === 'peer-open' || filterMode() === 'peer-closed';
  const buildItems = ({ applySearch }) => {
    const q = applySearch ? (search.value || '') : '';
    const showLocal = filterMode() === 'all' || filterMode() === 'local' || showsPeerOpen();
    const showServer = showsServer();
    const items = [];
    const all = store.list()
      .filter((m) => !applySearch || prefs.matchRow(m.name, m.keywords, q))
      .filter((m) => filterMode() !== 'peer-open' || isPeerOpen(m))
      .filter((m) => filterMode() !== 'peer-closed' || !isPeerOpen(m));
    const localLinked = all.filter((m) => isServerMeta(m));
    if (showLocal) for (const meta of all.filter((m) => !isServerMeta(m)))
      items.push({ key: localRowKey(meta), name: metaName(meta), date: metaDate(meta), isRemote: false, meta, build: () => makeRow(meta) });
    if (showServer) for (const meta of localLinked)
      items.push({ key: localRowKey(meta), name: metaName(meta), date: metaDate(meta), isRemote: false, meta, build: () => makeRow(meta) });
    // Deduped against server-linked local rows.
    if (showServer && Array.isArray(remotes.cache)) {
      const claimed = new Set(localLinked.map((m) => `${m.address}|${m.remoteId}`));
      for (const meta of remotes.cache) {
        if (claimed.has(`${meta.serverUrl}|${meta.id}`)) continue;
        if (applySearch && !prefs.matchRow(meta.name, meta.keywords, q)) continue;
        items.push({ key: remoteRowKey(meta), name: metaName(meta), date: metaDate(meta), isRemote: true, meta, build: () => makeRemoteRow(meta) });
      }
    }
    return items;
  };
  const sortItems = (items, mode) => sortProjectItems(items, mode, prefs.loadOrder());

  const rowPlan = () => {
    const q = search.value || '';
    const plan = [];
    const showIncog = filterMode() === 'all' || filterMode() === 'incognito';
    // The synthetic current-tab row, pinned above the sorted rows; in the incognito
    // filter only a real incognito session qualifies.
    if (showIncog && app.storage.temporary) {
      const label = app.storage.incognito ? 'incognito (unsaved)' : 'temporary (unsaved)';
      const qualifies = filterMode() === 'incognito' ? app.storage.incognito : true;
      if (qualifies && rowMatches(label, q))
        plan.push({ key: 'temp', build: () => makeRow(null, { temp: true, incognito: app.storage.incognito }) });
    }
    // Incognito sessions open in other tabs, also pinned above the sorted rows.
    if (showIncog) {
      for (const p of incognitoPeers())
        if (rowMatches(p.name || 'Incognito', q))
          plan.push({ key: `peer:${p.peerId ?? p.name}`, build: () => makeIncognitoPeerRow(p) });
    }
    for (const it of sortItems(buildItems({ applySearch: true }), prefs.sortMode()))
      plan.push({ key: it.key, item: it, build: it.build });
    return plan;
  };

  return { buildItems, sortItems, rowPlan, showsServer };
}
