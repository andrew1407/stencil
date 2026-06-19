// ── Opened-images ledger ─────────────────────────────────────────────────────
// The editor's projects live in its own origin's localStorage (unreadable here), so
// we mirror every image handed off in chrome.storage.local (keyed by source URL) to
// badge already-opened images and offer resume vs add-a-copy. matchEntries is tested.
export const LEDGER_KEY = 'stencil-opened';
const MAX_ENTRIES = 500;

const norm = (s) => String(s || '').trim();

// Only real, shareable image/video URLs are worth tracking: a data:/blob: still
// or an empty source can't be matched against another page's scan meaningfully.
export const trackableSource = (source) => {
  const s = norm(source);
  return s.startsWith('http:') || s.startsWith('https:');
};

// Pure: entries matching an image. Prefer exact source URL; fall back to filename
// only when no source is known (avoids same-named false-matches). Newest-first.
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

// Record one hand-off. Dedups on (source, resource, name): a repeat open refreshes the
// timestamp and bumps `count`. No-op (returns null) for untrackable sources. Newest
// sorts first; list is capped.
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

// ── Reconciliation (prune entries for deleted projects) ──────────────────────
// The editor is source of truth: a project removed there must stop badging here. A
// content script on the editor origin reports its live registry; we drop ledger
// entries with no matching project.

// Origin of a URL string, or '' when unparseable. Scopes reconciliation to the
// reporting editor so a second editor (e.g. a prod URL) can't prune the local one's entries.
export const originOf = (url) => {
  try { return new URL(String(url || '')).origin; } catch { return ''; }
};

// An entry is recorded BEFORE the editor tab saves its project, so a registry read at
// editor load can briefly lack it. Don't prune entries newer than this, so a fresh
// open never loses its badge.
export const RECONCILE_GRACE_MS = 2 * 60 * 1000;

// Pure: reconcile ledger entries against the reporting editor's live registry.
// `projects` = that editor's registry ([{ source }]). Only entries for `editorOrigin`
// are touched. Let `live` = live projects sharing an entry's source:
//   • live > 0  → keep, set `count` = live (so "opened N×" tracks reality both ways).
//   • live == 0 → drop, UNLESS newer than `graceMs` (editor may not have saved yet) or
//                 has no/untrackable source (can't reconcile) → keep as-is.
// Never adds or reorders; only drops or restamps `count`.
export const reconcileLedger = (entries, projects, editorOrigin, now = Date.now(), graceMs = RECONCILE_GRACE_MS) => {
  const list = Array.isArray(entries) ? entries : [];
  const org = norm(editorOrigin);
  // Live project count per source for this editor.
  const counts = new Map();
  for (const p of (Array.isArray(projects) ? projects : [])) {
    const s = norm(p && p.source);
    if (s) counts.set(s, (counts.get(s) || 0) + 1);
  }
  const out = [];
  for (const e of list) {
    if (originOf(e.editorUrl) !== org) { out.push(e); continue; }   // different editor → leave alone
    const src = norm(e.source);
    if (!src) { out.push(e); continue; }                            // can't reconcile → keep
    const live = counts.get(src) || 0;
    if (live > 0) { out.push(live === (e.count || 1) ? e : { ...e, count: live }); continue; }
    if (graceMs && (now - (Number(e.t) || 0)) < graceMs) { out.push(e); continue; } // too fresh → keep
    // stale: no live project for this source → drop.
  }
  return out;
};

// Reconcile the stored ledger against one editor's live registry and persist if
// changed. reconcile reuses the object for untouched entries, so same length +
// element-by-element reference match ⇔ no change. Best-effort; returns true when it wrote.
export const pruneLedger = async (projects, editorOrigin) => {
  const before = await loadLedger();
  const after = reconcileLedger(before, projects, editorOrigin);
  const changed = after.length !== before.length || after.some((e, i) => e !== before[i]);
  if (!changed) return false;
  try {
    await chrome.storage.local.set({ [LEDGER_KEY]: after });
  } catch {
    /* storage unavailable → keep stale badges rather than surface an error */
  }
  return true;
};
