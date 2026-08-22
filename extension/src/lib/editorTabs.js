// ── Editor-tab helpers (pure, dependency-free, unit-tested) ──────────────────
// The questions editor mode asks of a plain tab list — is this the editor, what's its row,
// which rows match the search, which tabs can we scan, may we import without asking.
import { originPattern } from './stencil.js';
import { matchesSearch } from './filters.js';
import { BLOCKED_SCHEMES } from './imageScan.js';

/**
 * Is this tab the configured Stencil editor? An ORIGIN match (the rule that also scopes the
 * editorBridge, so the two can't disagree); non-http(s) is never the editor. Only a PRE-filter:
 * an ordinary page beside the editor matches too, so callers confirm via a bridge round-trip.
 * @param {string} url - The tab's URL.
 * @param {string} editorUrl - The configured editor URL (settings.editorUrl).
 * @returns {boolean}
 */
export const isEditorTab = (url, editorUrl) => {
  const pattern = originPattern(url);
  return !!pattern && pattern === originPattern(editorUrl);
};

// A tab's host for the source-tab label ('' when the URL won't parse).
const hostOf = (url) => {
  try {
    return new URL(url).host;
  } catch {
    return '';
  }
};

/**
 * One "Open editors" row: a chrome tab joined with its EDITOR_STATE reply. `state` null = that
 * tab never answered (`ready:false`); everything is normalised so the UI never null-checks.
 * @param {object} tab - chrome.tabs.Tab ({id, windowId, url, title, active}).
 * @param {object|null} [state] - The EDITOR_STATE reply ({projectId, projectName, hasImage,
 *   imageName, imageSize:{w,h}, incognito, thumbnail, projects:[{id,name,active}]}).
 * @param {object} [opts]
 * @param {number} [opts.currentTabId] - The tab the panel / console call is standing in; that
 *   row is flagged `current` (it is the default import target and gets the "this tab" badge).
 * @returns {object} `{tabId, windowId, url, title, active, current, ready, projectId,
 *   projectName, hasImage, imageName, imageSize, incognito, thumbnail, projects}`.
 */
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
    // false = the bridge never answered → no preview, and importModeFor falls back to 'new'.
    ready: !!s,
    projectId: (s && s.projectId) || '',
    projectName: (s && s.projectName) || '',
    hasImage: !!(s && s.hasImage),
    imageName: (s && s.imageName) || '',
    imageSize: size && size.w > 0 && size.h > 0 ? { w: size.w, h: size.h } : null,
    // Never persisted by the editor, so importing over it loses work with no copy on disk.
    incognito: !!(s && s.incognito),
    thumbnail: (s && s.thumbnail) || '',
    projects: Array.isArray(s && s.projects)
      ? s.projects.filter(p => p && p.id != null).map(p => ({ id: p.id, name: p.name || '', active: !!p.active }))
      : [],
  };
};

/**
 * Filter editor rows by the search box, through lib/filters.js `matchesSearch` so the editor
 * list obeys the image list's rules exactly (project name / URL / title are its three fields).
 * @param {object[]} rows - Rows from `editorRow`.
 * @param {string} query - The search text ('' / undefined = no filtering).
 * @param {object} [opts]
 * @param {boolean} [opts.regex=false] - Treat `query` as a regular expression.
 * @returns {object[]} The matching rows, in order.
 */
export const matchEditors = (rows, query, { regex = false } = {}) =>
  (rows || []).filter(r => matchesSearch({ name: r.projectName, src: r.url, videoUrl: r.title }, { search: query, regex }));

/**
 * The tabs offered in "Images from another page": every open page we can actually scan, minus
 * blocked schemes, non-http(s) URLs and the editor's own tabs (an import's destination).
 * Which tabs are "the editor's own" is answerable two ways: `editorTabIds` (the tabs that
 * actually ANSWERED a state query) is authoritative — an ordinary page served from the editor's
 * origin is not an editor — and the origin rule is the fallback when no fan-out is to hand.
 * @param {object[]} tabs - chrome.tabs.Tab list (any window).
 * @param {object} [opts]
 * @param {string} [opts.editorUrl] - The configured editor URL; its tabs are excluded.
 * @param {number[]|Set<number>} [opts.editorTabIds] - Tab ids known to BE editors; when given,
 *   only these are excluded (so a same-origin ordinary page is correctly offered).
 * @returns {object[]} `{tabId, title, url, host, label}` in the given order, where `label` is
 *   the `<select>` text: "title — host", falling back to whichever of the two exists.
 */
export const sourceTabChoices = (tabs, { editorUrl, editorTabIds } = {}) => {
  const known = editorTabIds == null ? null : new Set(editorTabIds);
  const out = [];
  for (const tab of tabs || []) {
    const url = (tab && tab.url) || '';
    if (!tab || tab.id == null || !url) continue;
    if (BLOCKED_SCHEMES.some(s => url.startsWith(s))) continue;
    if (!originPattern(url)) continue;                 // null = not an http(s) page
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

/**
 * Narrow the source-tab choices by URL. The picker's filter is deliberately about the URL
 * alone — it's what tells two tabs of the same site apart — and `regex` treats the query as a
 * case-insensitive RegExp (an invalid pattern matches nothing, like the image list's).
 * @param {object[]} choices - Rows from `sourceTabChoices`.
 * @param {string} query - The filter text ('' / undefined = everything).
 * @param {object} [opts]
 * @param {boolean} [opts.regex=false]
 * @returns {object[]} The matching choices, in order.
 */
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

/**
 * How to import into an editor tab: `'new'` straight away, or `'ask'` (chooser first) when it
 * already holds an image a replace would overwrite. Anything unknown is `'new'` — the only mode
 * that can never destroy work.
 * @param {object|null} editorState - An EDITOR_STATE reply or an `editorRow` (both carry
 *   `hasImage`).
 * @returns {'new'|'ask'}
 */
export const importModeFor = (editorState) => (editorState && editorState.hasImage ? 'ask' : 'new');
