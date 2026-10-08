// The editor's projects live in its own origin's localStorage (unreadable here), so every
// hand-off is mirrored in chrome.storage.local, keyed by source URL.
import { writeChain, viaWorker } from './writeChain.js';

export const LEDGER_KEY = 'stencil-opened';
const MAX_ENTRIES = 500;

const norm = (s) => String(s || '').trim();

// A data:/blob: source cannot be matched against another page's scan.
export const trackableSource = (source) => {
  const s = norm(source);
  return s.startsWith('http:') || s.startsWith('https:');
};

// Filename matches only when no source is known, to avoid same-named false matches.
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

const ledgerTurn = writeChain();

const saveLedger = async (entries) => {
  try {
    await chrome.storage.local.set({ [LEDGER_KEY]: entries });
  } catch {
    /* storage full / unavailable → badges just won't show; not worth surfacing */
  }
};

// Dedups on (source, resource, name); a repeat open bumps `count`.
const applyRecordOpened = async ({ source, resource, name, editorUrl, t }) => {
  if (!trackableSource(source)) return null;
  const src = norm(source), res = norm(resource), nm = norm(name);
  return ledgerTurn(async () => {
    const entries = await loadLedger();
    const i = entries.findIndex(e =>
      norm(e.source) === src && norm(e.resource) === res && norm(e.name) === nm);
    const rec = {
      source: src, resource: res, name: nm,
      editorUrl: norm(editorUrl),
      t: t || Date.now(),
      count: i !== -1 ? (entries[i].count || 1) + 1 : 1,
    };
    const after = entries.filter((_, j) => j !== i);
    after.unshift(rec);
    if (after.length > MAX_ENTRIES) after.length = MAX_ENTRIES;
    await saveLedger(after);
    return rec;
  });
};

export const ledgerWrites = Object.freeze({ recordOpened: applyRecordOpened });
export const recordOpened = (rec) => viaWorker('ledger', 'recordOpened', [rec], applyRecordOpened);

export const lookup = async (source, name) => matchEntries(await loadLedger(), source, name);

// Reconciliation is scoped to the reporting editor's origin, so a second editor cannot
// prune the local one's entries.
export const originOf = (url) => {
  try { return new URL(String(url || '')).origin; } catch { return ''; }
};

// An entry is recorded BEFORE the editor tab saves its project, so a registry read at
// editor load can briefly lack it; entries younger than this are never pruned.
export const RECONCILE_GRACE_MS = 2 * 60 * 1000;

// Keeps an entry with live projects (restamping `count`), drops one without — unless it
// is within the grace period or has no source. Never adds or reorders.
export const reconcileLedger = (entries, projects, editorOrigin, now = Date.now(), graceMs = RECONCILE_GRACE_MS) => {
  const list = Array.isArray(entries) ? entries : [];
  const org = norm(editorOrigin);
  const counts = new Map();
  for (const p of (Array.isArray(projects) ? projects : [])) {
    const s = norm(p && p.source);
    if (s) counts.set(s, (counts.get(s) || 0) + 1);
  }
  const out = [];
  for (const e of list) {
    if (originOf(e.editorUrl) !== org) { out.push(e); continue; }
    const src = norm(e.source);
    if (!src) { out.push(e); continue; }
    const live = counts.get(src) || 0;
    if (live > 0) { out.push(live === (e.count || 1) ? e : { ...e, count: live }); continue; }
    if (graceMs && (now - (Number(e.t) || 0)) < graceMs) { out.push(e); continue; }
  }
  return out;
};

// reconcileLedger reuses the object for untouched entries, so a reference match ⇔ no change.
export const pruneLedger = (projects, editorOrigin) => ledgerTurn(async () => {
  const before = await loadLedger();
  const after = reconcileLedger(before, projects, editorOrigin);
  const changed = after.length !== before.length || after.some((e, i) => e !== before[i]);
  if (changed) await saveLedger(after);
  return changed;
});
