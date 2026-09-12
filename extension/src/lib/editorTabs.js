// The questions editor mode asks of a plain tab list.
import { originPattern } from './stencil.js';
import { matchesSearch } from './filters.js';
import { BLOCKED_SCHEMES } from './imageScan.js';

// An ORIGIN match (the rule that also scopes the editorBridge). Only a PRE-filter: an
// ordinary page beside the editor matches too, so callers confirm via a bridge round-trip.
export const isEditorTab = (url, editorUrl) => {
  const pattern = originPattern(url);
  return !!pattern && pattern === originPattern(editorUrl);
};

const hostOf = (url) => {
  try {
    return new URL(url).host;
  } catch {
    return '';
  }
};

// A chrome tab joined with its EDITOR_STATE reply; `state` null = never answered.
// `currentTabId` flags the row the panel stands in (the default import target).
export const editorRow = (tab, state, { currentTabId } = {}) => {
  const t = tab || {};
  const s = state || null;
  const size = (s && s.imageSize) || null;
  return {
    tabId: t.id != null ? t.id : null,
    windowId: t.windowId != null ? t.windowId : null,
    url: t.url || '',
    title: t.title || '',
    active: !!t.active,
    current: currentTabId != null && t.id === currentTabId,
    // false = the bridge never answered, so importModeFor falls back to 'new'.
    ready: !!s,
    projectId: (s && s.projectId) || '',
    projectName: (s && s.projectName) || '',
    hasImage: !!(s && s.hasImage),
    imageName: (s && s.imageName) || '',
    imageSize: size && size.w > 0 && size.h > 0 ? { w: size.w, h: size.h } : null,
    // Never persisted by the editor: importing over it loses work with no copy on disk.
    incognito: !!(s && s.incognito),
    thumbnail: (s && s.thumbnail) || '',
    projects: Array.isArray(s && s.projects)
      ? s.projects.filter(p => p && p.id != null).map(p => ({ id: p.id, name: p.name || '', active: !!p.active }))
      : [],
  };
};

// Through lib/filters.js `matchesSearch`, so the editor list obeys the image list's rules.
export const matchEditors = (rows, query, { regex = false } = {}) =>
  (rows || []).filter(r => matchesSearch({ name: r.projectName, src: r.url, videoUrl: r.title }, { search: query, regex }));

// Every scannable page minus the editor's own tabs. `editorTabIds` (tabs that ANSWERED a
// state query) is authoritative; the origin rule is the fallback when no fan-out is to hand.
export const sourceTabChoices = (tabs, { editorUrl, editorTabIds } = {}) => {
  const known = editorTabIds == null ? null : new Set(editorTabIds);
  const out = [];
  for (const tab of tabs || []) {
    const url = (tab && tab.url) || '';
    if (!tab || tab.id == null || !url) continue;
    if (BLOCKED_SCHEMES.some(s => url.startsWith(s))) continue;
    if (!originPattern(url)) continue;
    if (known ? known.has(tab.id) : isEditorTab(url, editorUrl)) continue;
    const host = hostOf(url);
    const title = (tab.title || '').trim();
    out.push({
      tabId: tab.id, title, url, host,
      favIconUrl: typeof tab.favIconUrl === 'string' ? tab.favIconUrl : '',
      label: title && host ? `${title} — ${host}` : (title || host || url),
    });
  }
  return out;
};

// By URL alone — what tells two tabs of the same site apart; an invalid regex matches nothing.
export const matchSourceTabs = (choices, query, { regex = false } = {}) => {
  const q = String(query || '').trim();
  if (!q) return (choices || []).slice();
  if (regex) {
    let re;
    try { re = new RegExp(q, 'i'); } catch { return []; }
    return (choices || []).filter(c => re.test(c.url || ''));
  }
  const needle = q.toLowerCase();
  return (choices || []).filter(c => String(c.url || '').toLowerCase().includes(needle));
};

// 'ask' (chooser first) when the tab already holds an image; anything unknown is 'new',
// the only mode that can never destroy work.
export const importModeFor = (editorState) => (editorState && editorState.hasImage ? 'ask' : 'new');
