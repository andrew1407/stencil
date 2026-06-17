// ── Opened-images ledger ─────────────────────────────────────────────────────
// The editor stores its projects in its own origin's localStorage, which the
// extension (a different origin) can't read. So to tell which page images already
// have an editor, we keep our OWN record here in chrome.storage.local: every image
// the extension hands to the editor is appended, keyed by its real source URL.
//
// The popup uses lookup() to badge already-opened images and to offer "resume"
// (re-open, the editor switches to the matching project) vs "add a new copy".
// Pure matching (matchEntries) is unit-tested; the rest wrap chrome.storage.
export const LEDGER_KEY = 'stencil-opened';
const MAX_ENTRIES = 500;

const norm = (s) => String(s || '').trim();

// Only real, shareable image/video URLs are worth tracking: a data:/blob: still
// or an empty source can't be matched against another page's scan meaningfully.
export const trackableSource = (source) => {
  const s = norm(source);
  return s.startsWith('http:') || s.startsWith('https:');
};

// Pure: entries matching an image. Prefer the exact source URL; fall back to the
// filename only when no source is known (so same-named files don't false-match
// when a real URL is available). Most-recent first (entries are stored newest-first).
export const matchEntries = (entries, source, name) => {
  const list = Array.isArray(entries) ? entries : [];
  const src = norm(source), nm = norm(name);
  if (src) return list.filter(e => norm(e.source) === src);
  return nm ? list.filter(e => !norm(e.source) && norm(e.name) === nm) : [];
};

export const loadLedger = async () => {
  try {
    const o = await chrome.storage.local.get(LEDGER_KEY);
    return Array.isArray(o[LEDGER_KEY]) ? o[LEDGER_KEY] : [];
  } catch {
    return [];
  }
};

// Record one hand-off. Dedups on (source, resource, name): a repeat open refreshes
// the timestamp and bumps `count` rather than piling up duplicates. No-op (returns
// null) for untrackable sources. Newest entries sort first; the list is capped.
export const recordOpened = async ({ source, resource, name, editorUrl, t }) => {
  if (!trackableSource(source)) return null;
  const src = norm(source), res = norm(resource), nm = norm(name);
  const entries = await loadLedger();
  const i = entries.findIndex(e =>
    norm(e.source) === src && norm(e.resource) === res && norm(e.name) === nm);
  const rec = {
    source: src, resource: res, name: nm,
    editorUrl: norm(editorUrl),
    t: t || Date.now(),
    count: i !== -1 ? (entries[i].count || 1) + 1 : 1,
  };
  if (i !== -1) entries.splice(i, 1);
  entries.unshift(rec);
  if (entries.length > MAX_ENTRIES) entries.length = MAX_ENTRIES;
  try {
    await chrome.storage.local.set({ [LEDGER_KEY]: entries });
  } catch {
    /* storage full / unavailable → badges just won't show; not worth surfacing */
  }
  return rec;
};

export const lookup = async (source, name) => matchEntries(await loadLedger(), source, name);
