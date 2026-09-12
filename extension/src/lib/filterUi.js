// The panel's filter controls: read into the shape passesFilters takes, the per-format
// pills, and persistence in chrome.storage.local (the popup's DOM is rebuilt each open).
import { distinctFormats, formatOfItem, UNKNOWN_FORMAT, VIDEO_FORMATS } from './filters.js';

// Always offered, ahead of whatever else the page uses.
export const COMMON_FORMATS = ['png', 'jpg', 'gif', 'webp', 'svg', 'avif', 'bmp', 'ico', 'tiff'];

export const FILTERS_KEY = 'popupFilters';

// Common formats, video containers, the page's extras, then 'etc' last.
export const formatListFor = (items) => {
  const present = new Set(distinctFormats(items));
  if (items.some((it) => !formatOfItem(it))) present.add(UNKNOWN_FORMAT);
  const known = new Set([...COMMON_FORMATS, ...VIDEO_FORMATS, UNKNOWN_FORMAT]);
  const extras = [...present].filter((f) => !known.has(f));
  return { formats: [...COMMON_FORMATS, ...VIDEO_FORMATS, ...extras, UNKNOWN_FORMAT], present };
};

export const formatPillsHtml = (formats, present) => formats.map((f) => {
  const absent = !present.has(f);
  return `<label class="chk${absent ? ' absent' : ''}"${absent ? ' data-title="Not present on this page"' : ''}>`
    + `<input type="checkbox" value="${f}" checked>${f.toUpperCase()}</label>`;
}).join('');

export const createFilterUi = ({ doc = document, onChange = () => {} } = {}) => {
  let persisted = null;
  let lastSavedJson = null;   // tells a storage.onChanged echo of our own write from another surface's

  const el = (id) => doc.getElementById(id);
  const checkboxes = () => [...el('f-formats').querySelectorAll('input')];
  const allChecked = () => {
    const cbs = checkboxes();
    return cbs.length > 0 && cbs.every((c) => c.checked);
  };
  const updateToggleLabel = () => {
    el('f-fmt-toggle').textContent = allChecked() ? 'Deselect all' : 'Select all';
  };

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

  // Formats new since the save default on.
  const applyPersistedFormats = () => {
    const box = el('f-formats');
    if (!box) return;
    const off = new Set(persisted && Array.isArray(persisted.disabledFormats) ? persisted.disabledFormats : []);
    box.querySelectorAll('input').forEach((cb) => { cb.checked = !off.has(cb.value); });
    updateToggleLabel();
  };

  // The pills are rebuilt on every scan, so the persisted OFF set is re-applied here.
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
      disabledFormats: checkboxes().filter((c) => !c.checked).map((c) => c.value),
    };
    lastSavedJson = JSON.stringify(persisted);
    // Absorbs sync and async failures alike (a torn-down extension context).
    try { Promise.resolve(chrome.storage.local.set({ [FILTERS_KEY]: persisted })).catch(() => {}); }
    catch { /* storage unavailable */ }
  };

  const restoreStatic = () => {
    if (!persisted) return;
    const f = persisted;
    const setV = (id, v) => { const c = el(id); if (c) c.value = v == null ? '' : v; };
    setV('f-search', f.search); setV('f-minw', f.minW); setV('f-maxw', f.maxW); setV('f-minh', f.minH); setV('f-maxh', f.maxH);
    const setC = (id, v) => { const c = el(id); if (c && typeof v === 'boolean') c.checked = v; };
    setC('f-regex', f.regex);
    setC('f-img', f.includeImg); setC('f-bg', f.includeBg); setC('f-video', f.includeVideo); setC('f-poster', f.includePosters); setC('f-meta', f.includeMeta);
  };

  // Returns whether it applied (the owner then re-filters its list).
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
