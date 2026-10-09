// Shared extension helpers; service-worker-safe (no FileReader / DOM).
import { mountStencilModal } from './drop/overlay.js';
import { loadShellTheme } from './prefs/shellTheme.js';
import { MSG } from './messages.js';
import { editorOriginPattern } from './prefs/settings.js';

export {
  DEFAULT_EDITOR_URL, DEFAULT_PAGE, editorOriginPattern, getSettings, originPattern, setSettings,
} from './prefs/settings.js';
export {
  blobToDataUrl, fetchAsDataUrl, filenameFromUrl, guessMime, isImageDataUrl,
} from './image/data.js';
export {
  MAX_PAYLOAD, buildHandoff, buildLaunchUrl, launchEditorModal, openEditorTab,
} from './menu/editorLaunch.js';

// Selecting a tab in a background window leaves it hidden, hence the second call.
export const focusTab = async (tab) => {
  await chrome.tabs.update(tab.id, { active: true });
  if (tab.windowId != null) await chrome.windows.update(tab.windowId, { focused: true });
};

// False = no open editor tab took it; the caller opens a fresh tab.
export const resumeInOpenEditor = async ({ source, name }) => {
  const pattern = await editorOriginPattern();
  if (!pattern) return false;
  try {
    const [tab] = await chrome.tabs.query({ url: [pattern] });
    if (!tab || tab.id == null) return false;
    await focusTab(tab);
    // Throws when no editorBridge is listening (a tab we could not inject).
    await chrome.tabs.sendMessage(tab.id, { type: MSG.EDITOR_SWITCH, source, name });
    return true;
  } catch {
    return false;
  }
};


// One session entry per launch, named by the nonce in the crop page's `?k=`: two launches never
// share it, and the page removes it once read.
export const CROP_KEY_PREFIX = 'stencil-crop:';
// ms an unread entry is kept (its page never opened); swept by the next launch.
const CROP_ENTRY_TTL_MS = 60_000;

const sweepStaleCrops = async (now) => {
  try {
    const all = await chrome.storage.session.get(null);
    const stale = Object.keys(all).filter((k) => k.startsWith(CROP_KEY_PREFIX) && !(now - (all[k]?.t || 0) < CROP_ENTRY_TTL_MS));
    if (stale.length) await chrome.storage.session.remove(stale);
  } catch { /* the set below reports a storage that does not answer */ }
};

// Falls back to a real tab when the modal cannot be injected or the frame is CSP-blocked. A
// refused `set` (over quota) still opens the page, with `?error=` saying why it has no image.
export const launchCrop = async ({ src, source, resource, tabId }) => {
  const nonce = crypto.randomUUID();
  const params = new URLSearchParams({ k: nonce });
  const now = Date.now();
  await sweepStaleCrops(now);
  try {
    await chrome.storage.session.set({ [CROP_KEY_PREFIX + nonce]: { src, source: source || '', resource: resource || '', t: now } });
  } catch (err) {
    params.set('error', err?.message || String(err));
  }
  const url = `${chrome.runtime.getURL('src/crop/crop.html')}?${params}`;
  if (tabId == null) return chrome.tabs.create({ url });
  try {
    // The 8000 ms watchdog only catches a CSP-blocked frame; a short one would close a
    // working modal mid-load and re-open the crop page in a tab.
    await chrome.scripting.executeScript({
      target: { tabId }, world: 'ISOLATED', func: mountStencilModal, args: [url, 'Quick crop', 8000, await loadShellTheme()]
    });
  } catch {
    return chrome.tabs.create({ url });
  }
};

// The crop page's side: `?src=` wins, else the launch's entry, read once and removed.
export const takeCropHandoff = async (search) => {
  const params = new URLSearchParams(search);
  const key = params.get('k') ? CROP_KEY_PREFIX + params.get('k') : '';
  let entry = {};
  if (key) {
    try {
      entry = (await chrome.storage.session.get(key))[key] || {};
      await chrome.storage.session.remove(key);
    } catch { /* no entry → the page says there is no image */ }
  }
  return {
    src: params.get('src') || entry.src || '',
    source: entry.source || '',
    resource: entry.resource || '',
    error: params.get('error') || '',
  };
};
