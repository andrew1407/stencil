// When this browser last opened each local project (epoch ms), behind the projects list's "Recently
// opened" orders. App-wide in localStorage, never part of a project or a server. Desktop twin:
// io/fileStore.hpp markProjectOpened.

export const PROJECT_OPENED_KEY = 'drawingApp_projectOpened';
// The newest entries kept; an older one reads as never opened, last in "Recently opened".
export const PROJECT_OPENED_CAP = 500;

const ls = () => (typeof localStorage !== 'undefined' ? localStorage : null);

const readAll = () => {
  try {
    const got = JSON.parse(ls()?.getItem(PROJECT_OPENED_KEY) || '{}');
    return got && typeof got === 'object' && !Array.isArray(got) ? got : {};
  } catch { return {}; }
};

// 0 for a project never opened here.
export const projectOpenedAt = (id, all = readAll()) => {
  const at = Number(all[id]);
  return Number.isFinite(at) && at > 0 ? at : 0;
};

// The whole table once, for a list that sorts many rows.
export const projectOpenedTable = () => readAll();

export const markProjectOpened = (id, now = Date.now()) => {
  if (id == null) return;
  const all = readAll();
  all[id] = now;
  const keep = Object.entries(all).sort((a, b) => b[1] - a[1]).slice(0, PROJECT_OPENED_CAP);
  try { ls()?.setItem(PROJECT_OPENED_KEY, JSON.stringify(Object.fromEntries(keep))); } catch { /* storage full or blocked: the order just stays put */ }
};
