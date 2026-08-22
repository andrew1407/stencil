// ── The panel's filter controls: read, format pills, persistence ─────────────
// Extracted from popup.js: reading the filter controls into the shape passesFilters
// takes, building the per-format .chk pills, and persisting the whole filter state so
// it survives the popup closing (its DOM is rebuilt each open) and mirrors across the
// concurrently-open surfaces. The owner passes its document and an onChange (its
// applyFilters); chrome.storage.local holds the persisted state.
import { distinctFormats, formatOfItem, UNKNOWN_FORMAT, VIDEO_FORMATS } from './filters.js';

// Common web image formats always offered in the filter, plus any others the page
// uses and the video container formats (VIDEO_FORMATS).
export const COMMON_FORMATS = ['png', 'jpg', 'gif', 'webp', 'svg', 'avif', 'bmp', 'ico', 'tiff'];

export const FILTERS_KEY = 'popupFilters';

/**
 * The ordered pill list for the scanned items: common formats, video containers, any
 * extra formats the page uses, then 'etc' (undetectable) last — plus which of them are
 * actually present on the page.
 */
export const formatListFor = (items) => {
  const present = new Set(distinctFormats(items));
  // 'etc' is always offered, last, marked present only when the page has such items.
  if (items.some((it) => !formatOfItem(it))) present.add(UNKNOWN_FORMAT);
  const known = new Set([...COMMON_FORMATS, ...VIDEO_FORMATS, UNKNOWN_FORMAT]);
  const extras = [...present].filter((f) => !known.has(f));
  return { formats: [...COMMON_FORMATS, ...VIDEO_FORMATS, ...extras, UNKNOWN_FORMAT], present };
};

/** One `.chk` pill per format; a format absent from the page is dimmed and titled. */
export const formatPillsHtml = (formats, present) => formats.map((f) => {
  const absent = !present.has(f);
  return `<label class="chk${absent ? ' absent' : ''}"${absent ? ' title="Not present on this page"' : ''}>`
    + `<input type="checkbox" value="${f}" checked>${f.toUpperCase()}</label>`;
}).join('');

/**
 * Wire the filter controls of one surface.
 *
 * @param {object} deps
 * @param {Document} [deps.doc]
 * @param {() => void} [deps.onChange] - The owner's applyFilters, run when a pill changes.
 */
export const createFilterUi = ({ doc = document, onChange = () => {} } = {}) => {
  let persisted = null;
  // JSON of the state we last wrote, so a storage.onChanged echo of our own write can
  // be told from another surface's (avoids a loop).
  let lastSavedJson = null;

  const el = (id) => doc.getElementById(id);
  const checkboxes = () => [...el('f-formats').querySelectorAll('input')];
  const allChecked = () => {
    const cbs = checkboxes();
    return cbs.length > 0 && cbs.every((c) => c.checked);
  };
  const updateToggleLabel = () => {
    el('f-fmt-toggle').textContent = allChecked() ? 'Deselect all' : 'Select all';
  };

  // The current controls in the shape passesFilters takes.
  const read = () => {
    const num = (control) => {
      const v = parseFloat(control.value);
      return isNaN(v) ? null : v;
    };
    return {
      search: el('f-search').value.trim(),
      regex: el('f-regex').checked,
      formats: checkboxes().filter((c) => c.checked).map((c) => c.value),
      minW: num(el('f-minw')),
      maxW: num(el('f-maxw')),
      minH: num(el('f-minh')),
      maxH: num(el('f-maxh')),
      includeImg: el('f-img').checked,
      includeBg: el('f-bg').checked,
      includeVideo: el('f-video').checked,
      includePosters: el('f-poster').checked,
      includeMeta: el('f-meta').checked,
    };
  };

  // Sync the (already-rendered) pills to the persisted disabledFormats: OFF formats
  // unchecked, everything else (incl. formats new since the save) checked.
  const applyPersistedFormats = () => {
    const box = el('f-formats');
    if (!box) return;
    const off = new Set(persisted && Array.isArray(persisted.disabledFormats) ? persisted.disabledFormats : []);
    box.querySelectorAll('input').forEach((cb) => { cb.checked = !off.has(cb.value); });
    updateToggleLabel();
  };

  // Build the pills (all checked = no filtering), then re-apply the persisted OFF set —
  // the pills are rebuilt fresh on every scan.
  const populateFormats = (items) => {
    const { formats, present } = formatListFor(items);
    const box = el('f-formats');
    box.innerHTML = formatPillsHtml(formats, present);
    box.querySelectorAll('input').forEach((cb) =>
      cb.addEventListener('change', () => {
        updateToggleLabel();
        onChange();
      }));
    applyPersistedFormats();
  };

  const load = async () => {
    try { persisted = (await chrome.storage.local.get(FILTERS_KEY))[FILTERS_KEY] || null; }
    catch { persisted = null; }
    lastSavedJson = persisted ? JSON.stringify(persisted) : null;
  };

  const save = () => {
    const f = read();
    persisted = {
      search: f.search, regex: f.regex, minW: f.minW, maxW: f.maxW, minH: f.minH, maxH: f.maxH,
      includeImg: f.includeImg, includeBg: f.includeBg, includeVideo: f.includeVideo, includePosters: f.includePosters, includeMeta: f.includeMeta,
      disabledFormats: checkboxes().filter((c) => !c.checked).map((c) => c.value),   // store the OFF ones (new formats default on)
    };
    lastSavedJson = JSON.stringify(persisted);
    // Fire-and-forget, absorbing sync AND async failures (torn-down extension context).
    try { Promise.resolve(chrome.storage.local.set({ [FILTERS_KEY]: persisted })).catch(() => {}); }
    catch { /* storage unavailable */ }
  };

  // Restore the static controls (the pills are restored in populateFormats, since
  // they're rebuilt on every scan).
  const restoreStatic = () => {
    if (!persisted) return;
    const f = persisted;
    const setV = (id, v) => { const c = el(id); if (c) c.value = v == null ? '' : v; };
    setV('f-search', f.search); setV('f-minw', f.minW); setV('f-maxw', f.maxW); setV('f-minh', f.minH); setV('f-maxh', f.maxH);
    const setC = (id, v) => { const c = el(id); if (c && typeof v === 'boolean') c.checked = v; };
    setC('f-regex', f.regex);
    setC('f-img', f.includeImg); setC('f-bg', f.includeBg); setC('f-video', f.includeVideo); setC('f-poster', f.includePosters); setC('f-meta', f.includeMeta);
  };

  // Another surface changed the persisted filters: adopt the new value and re-sync the
  // controls, unless it is only the echo of our own write. Returns whether it applied
  // (the owner then re-filters its list).
  const acceptExternal = (nv) => {
    if (!nv || JSON.stringify(nv) === lastSavedJson) return false;
    persisted = nv;
    lastSavedJson = JSON.stringify(nv);
    restoreStatic();
    applyPersistedFormats();
    return true;
  };

  return { read, save, load, restoreStatic, populateFormats, applyPersistedFormats, checkboxes, allChecked, updateToggleLabel, acceptExternal };
};
