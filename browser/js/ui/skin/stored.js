// The skin on when the app last closed, re-worn at boot over the stored motion and accent.
// Extension twin: lib/prefs/shellPrefs.js stencil_skin; desktop twin: fileStore::loadSkin.
export const SKIN_STORAGE_KEY = 'drawingApp_skin';
const NAMES = new Set(['webcore', 'aqua']);

// The stored skin, or null (none, junk, or storage blocked).
export const storedSkin = () => {
  try {
    const name = localStorage.getItem(SKIN_STORAGE_KEY);
    return NAMES.has(name) ? name : null;
  } catch {
    return null;
  }
};

// `name` null forgets it.
export const storeSkin = (name) => {
  try {
    if (NAMES.has(name)) localStorage.setItem(SKIN_STORAGE_KEY, name);
    else localStorage.removeItem(SKIN_STORAGE_KEY);
  } catch { /* storage blocked: the skin stays for this session */ }
};
