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
  list.unshift({
    source: norm(rec.source), site: norm(rec.site), resource: norm(rec.resource),
    name: norm(rec.name), kind: norm(rec.kind) || 'image', t: rec.t || Date.now(),
  });
  if (list.length > MAX_PINS) list.length = MAX_PINS;
  return list;
};

// Pure: remove the pin for (site, source); returns a new list (same ref array when
// nothing matched, so callers can skip a needless write).
export const removePinEntry = (entries, site, source) => {
  const list = Array.isArray(entries) ? entries : [];
  const k = pinKey(site, source);
  const out = list.filter((e) => pinKey(e.site, e.source) !== k);
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

// Pin (pinned=true) or unpin (pinned=false) one source on a site, persisting the
// result. No-op write when unpinning something that wasn't pinned. Returns the new list.
export const setPinned = async ({ source, site, resource, name, kind, pinned }) => {
  const src = norm(source);
  if (!src) return loadPins();   // nothing openable to key on
  const before = await loadPins();
  const after = pinned
    ? addPinEntry(before, { source: src, site, resource, name, kind })
    : removePinEntry(before, site, src);
  if (after !== before) await savePins(after);
  return after;
};
