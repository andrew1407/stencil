// ── Pinned-images store ──────────────────────────────────────────────────────
// User-pinned images/videos, kept in chrome.storage.local keyed by (site origin,
// source URL). Pinned items float to the top of the popup list (gray outline) and
// are browsable across every site from the options page. Independent of the opened
// ledger (lib/ledger.js, mirrored here): an image can be pinned, edited, both, or
// neither. The pure helpers (siteOf, isPinnedIn, addPinEntry, …) are unit-tested.
export const PINS_KEY = 'stencil-pinned';
const MAX_PINS = 500;

const norm = (s) => String(s || '').trim();

// Origin of a page URL, or '' when unparseable — the "site" a pin is grouped under
// (mirrors ledger.js originOf). A pin's site is the page it was pinned on, not the
// image's own host, so the options page can group "pins on example.com".
export const siteOf = (url) => {
  try { return new URL(norm(url)).origin; } catch { return ''; }
};

// Stable identity of a pin: its page origin + the image/video source URL. Two scans
// of the same image on the same site collapse to one pin.
export const pinKey = (site, source) => `${norm(site)}\n${norm(source)}`;

// Pure: is (site, source) currently pinned in this entry list?
export const isPinnedIn = (entries, site, source) => {
  const k = pinKey(site, source);
  return (Array.isArray(entries) ? entries : []).some((e) => pinKey(e.site, e.source) === k);
};

// Pure: every pin recorded for one site (newest-first order is preserved).
export const matchPinsForSite = (entries, site) => {
  const s = norm(site);
  return (Array.isArray(entries) ? entries : []).filter((e) => norm(e.site) === s);
};

// Pure: the distinct sites (origins) that have at least one pin, newest-pin-first so
// the options dropdown lists the most-recently-used sites at the top.
export const sitesOf = (entries) => {
  const out = [];
  const seen = new Set();
  for (const e of (Array.isArray(entries) ? entries : [])) {
    const s = norm(e.site);
    if (s && !seen.has(s)) { seen.add(s); out.push(s); }
  }
  return out;
};

// Pure: add a pin, deduped on (site, source). A repeat pin refreshes the timestamp
// and floats to the front. Newest sorts first; the list is capped at MAX_PINS.
export const addPinEntry = (entries, rec) => {
  const list = (Array.isArray(entries) ? entries : []).slice();
  const k = pinKey(rec.site, rec.source);
  const i = list.findIndex((e) => pinKey(e.site, e.source) === k);
  if (i !== -1) list.splice(i, 1);
  const kind = norm(rec.kind) || 'image';
  const entry = {
    source: norm(rec.source), site: norm(rec.site), resource: norm(rec.resource),
    name: norm(rec.name), kind, t: rec.t || Date.now(),
  };
  // Only project pins carry the custom accent `color` (the popup paints the name with it);
  // plain image/video pins never do.
  if (kind === 'project') entry.color = norm(rec.color);
  list.unshift(entry);
  if (list.length > MAX_PINS) list.length = MAX_PINS;
  return list;
};

// Pure: a pinned project's name colour — its custom `color`, or `fallback` when empty/unset.
export const projectNameColor = (color, fallback) => norm(color) || fallback;

// Pure: remove the pin for (site, source); returns a new list (same ref array when
// nothing matched, so callers can skip a needless write).
export const removePinEntry = (entries, site, source) => {
  const list = Array.isArray(entries) ? entries : [];
  const k = pinKey(site, source);
  const out = list.filter((e) => pinKey(e.site, e.source) !== k);
  return out.length === list.length ? list : out;
};

// Pure: drop every pin for one site (origin); returns a new list (same ref array when
// nothing matched). Underpins the options page's scoped "Clear" of a single site.
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

// Serialize every load→modify→save so concurrent pin/unpin calls can't clobber each
// other. chrome.storage has no atomic read-modify-write, so without this a tight
// `stencil.pin([...])` batch (or a popup + context-menu race) would lose writes: every
// call reads the same `before`, and the last set() wins — only one pin survives. Each
// mutation chains off the previous one's completed write before it reads.
let pinWriteChain = Promise.resolve();

// Pin (pinned=true) or unpin (pinned=false) one source on a site, persisting the
// result. No-op write when unpinning something that wasn't pinned. Returns the new list.
export const setPinned = async ({ source, site, resource, name, kind, pinned }) => {
  const src = norm(source);
  if (!src) return loadPins();   // nothing openable to key on
  const run = pinWriteChain.then(async () => {
    const before = await loadPins();
    const after = pinned
      ? addPinEntry(before, { source: src, site, resource, name, kind })
      : removePinEntry(before, site, src);
    if (after !== before) await savePins(after);
    return after;
  });
  // Keep the queue alive even if one op rejects, so a single failure can't wedge it.
  pinWriteChain = run.catch(() => {});
  return run;
};

// Clear pins in bulk, serialized through the same write chain as setPinned so it can't
// clobber a concurrent pin/unpin. `site` of 'all' (or empty) wipes every pin; a specific
// origin wipes only that site's pins. Returns the new list.
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
