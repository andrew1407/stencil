// User pins in chrome.storage.local, keyed by (site origin, source URL); independent of
// the opened ledger (lib/ledger.js).
export const PINS_KEY = 'stencil-pinned';
const MAX_PINS = 500;

const norm = (s) => String(s || '').trim();

// A pin's site is the page it was pinned on, not the image's own host (mirrors ledger.js originOf).
export const siteOf = (url) => {
  try { return new URL(norm(url)).origin; } catch { return ''; }
};

export const pinKey = (site, source) => `${norm(site)}\n${norm(source)}`;

export const isPinnedIn = (entries, site, source) => {
  const k = pinKey(site, source);
  return (Array.isArray(entries) ? entries : []).some((e) => pinKey(e.site, e.source) === k);
};

export const matchPinsForSite = (entries, site) => {
  const s = norm(site);
  return (Array.isArray(entries) ? entries : []).filter((e) => norm(e.site) === s);
};

// Newest-pin-first, so the options dropdown lists the most recently used sites first.
export const sitesOf = (entries) => {
  const out = [];
  const seen = new Set();
  for (const e of (Array.isArray(entries) ? entries : [])) {
    const s = norm(e.site);
    if (s && !seen.has(s)) { seen.add(s); out.push(s); }
  }
  return out;
};

// Trim, drop blanks, dedupe case-insensitively — the browser store and server do the same.
export const normalizeKeywords = (keywords) => {
  const out = [];
  const seen = new Set();
  for (const raw of (Array.isArray(keywords) ? keywords : [])) {
    const k = norm(raw);
    if (!k) continue;
    const lk = k.toLowerCase();
    if (seen.has(lk)) continue;
    seen.add(lk);
    out.push(k);
  }
  return out;
};

export const pinKeywords = (pin) => (pin && Array.isArray(pin.keywords)) ? pin.keywords : [];

export const PIN_SEARCH_MODES = ['common', 'names', 'keywords'];

// Empty query matches everything; case-insensitive substring per the mode.
export const pinMatchesSearch = (pin, query, mode = 'common') => {
  const q = norm(query).toLowerCase();
  if (!q) return true;
  const name = norm(pin && pin.name).toLowerCase();
  const kw = pinKeywords(pin).join(' ').toLowerCase();
  if (mode === 'names') return name.includes(q);
  if (mode === 'keywords') return kw.includes(q);
  return name.includes(q) || kw.includes(q);
};

// A repeat pin floats to the front; a re-pin without `keywords` keeps the existing ones.
export const addPinEntry = (entries, rec) => {
  const list = (Array.isArray(entries) ? entries : []).slice();
  const k = pinKey(rec.site, rec.source);
  const i = list.findIndex((e) => pinKey(e.site, e.source) === k);
  let prevKeywords = null;
  if (i !== -1) { prevKeywords = list[i].keywords; list.splice(i, 1); }
  const kind = norm(rec.kind) || 'image';
  const entry = {
    source: norm(rec.source), site: norm(rec.site), resource: norm(rec.resource),
    name: norm(rec.name), kind, t: rec.t || Date.now(),
  };
  // Only project pins carry the custom accent `color`.
  if (kind === 'project') entry.color = norm(rec.color);
  const kws = rec.keywords !== undefined ? normalizeKeywords(rec.keywords)
    : (Array.isArray(prevKeywords) ? prevKeywords : []);
  if (kws.length) entry.keywords = kws;
  list.unshift(entry);
  if (list.length > MAX_PINS) list.length = MAX_PINS;
  return list;
};

export const projectNameColor = (color, fallback) => norm(color) || fallback;

// Returns the same array when nothing matched, so callers can skip the write.
export const removePinEntry = (entries, site, source) => {
  const list = Array.isArray(entries) ? entries : [];
  const k = pinKey(site, source);
  const out = list.filter((e) => pinKey(e.site, e.source) !== k);
  return out.length === list.length ? list : out;
};

export const removeSiteEntries = (entries, site) => {
  const s = norm(site);
  const list = Array.isArray(entries) ? entries : [];
  const out = list.filter((e) => norm(e.site) !== s);
  return out.length === list.length ? list : out;
};

export const loadPins = async () => {
  try {
    const o = await chrome.storage.local.get(PINS_KEY);
    return Array.isArray(o[PINS_KEY]) ? o[PINS_KEY] : [];
  } catch {
    return [];
  }
};

const savePins = async (entries) => {
  try {
    await chrome.storage.local.set({ [PINS_KEY]: entries });
  } catch {
    /* storage full / unavailable → pin just won't persist; not worth surfacing */
  }
};

// chrome.storage has no atomic read-modify-write: every mutation chains off the previous
// one's completed write before it reads, or concurrent pins would lose writes.
let pinWriteChain = Promise.resolve();

export const setPinned = async ({ source, site, resource, name, kind, keywords, pinned }) => {
  const src = norm(source);
  if (!src) return loadPins();   // nothing openable to key on
  const run = pinWriteChain.then(async () => {
    const before = await loadPins();
    const after = pinned
      ? addPinEntry(before, { source: src, site, resource, name, kind, keywords })
      : removePinEntry(before, site, src);
    if (after !== before) await savePins(after);
    return after;
  });
  // A rejected op must not wedge the queue.
  pinWriteChain = run.catch(() => {});
  return run;
};

export const setPinKeywords = async (site, source, keywords) => {
  const src = norm(source);
  const run = pinWriteChain.then(async () => {
    const before = await loadPins();
    const k = pinKey(site, src);
    const i = (Array.isArray(before) ? before : []).findIndex((e) => pinKey(e.site, e.source) === k);
    if (i === -1) return before;
    const kws = normalizeKeywords(keywords);
    const after = before.slice();
    const entry = { ...after[i] };
    if (kws.length) entry.keywords = kws; else delete entry.keywords;
    after[i] = entry;
    await savePins(after);
    return after;
  });
  pinWriteChain = run.catch(() => {});
  return run;
};

// `site` of 'all' (or empty) wipes every pin.
export const clearPins = async (site) => {
  const all = !norm(site) || norm(site) === 'all';
  const run = pinWriteChain.then(async () => {
    const before = await loadPins();
    const after = all ? [] : removeSiteEntries(before, site);
    if (after !== before && !(all && before.length === 0)) await savePins(after);
    return after;
  });
  pinWriteChain = run.catch(() => {});
  return run;
};
