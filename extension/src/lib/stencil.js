// ── Shared extension helpers ────────────────────────────────────────────────
// The pure helpers (buildLaunchUrl, filenameFromUrl, guessMime) are unit-tested;
// the rest wrap chrome.* and are service-worker-safe (no FileReader / DOM).import { mountStencilModal } from './overlay.js';
import { loadShellTheme } from './shellTheme.js';
import { MSG } from './messages.js';
import { editorOriginPattern } from './settings.js';

export {
  DEFAULT_EDITOR_URL, DEFAULT_PAGE, editorOriginPattern, getSettings, originPattern, setSettings,
} from './settings.js';
export {
  blobToDataUrl, fetchAsDataUrl, filenameFromUrl, guessMime, isImageDataUrl,
} from './imageData.js';
export {
  MAX_PAYLOAD, buildHandoff, buildLaunchUrl, launchEditorModal, openEditorTab,
} from './editorLaunch.js';

export const focusTab = async (tab) => {
  await chrome.tabs.update(tab.id, { active: true });
  if (tab.windowId != null) await chrome.windows.update(tab.windowId, { focused: true });
};

// Resume an already-opened image in the editor tab that's ALREADY open: focus that tab
// and ask its (same-origin) editorBridge to switch to the matching project — no
// navigation, nothing lost. False = fall back to opening a new tab.
export const resumeInOpenEditor = async ({ source, name }) => {
  const pattern = await editorOriginPattern();
  if (!pattern) return false;
  try {
    const [tab] = await chrome.tabs.query({ url: [pattern] });
    if (!tab || tab.id == null) return false;
    await focusTab(tab);
    // Throws if no editorBridge is listening (e.g. a very old tab we couldn't inject) →
    // caller falls back to a fresh tab.
    await chrome.tabs.sendMessage(tab.id, { type: MSG.EDITOR_SWITCH, source, name });
    return true;
  } catch {
    return false;
  }
};


export const CROP_SRC_KEY = 'stencil-crop-src';
// Provenance (source/resource URLs) for the image being cropped, threaded through
// to the post-crop editor hand-off so a cropped image keeps where it came from.
export const CROP_META_KEY = 'stencil-crop-meta';

// Open the quick-crop tool as a small in-page modal on the given tab (falls back to a
// real tab when the modal can't be injected / the frame is CSP-blocked). `source`/
// `resource` ride along via session storage so the cropped result keeps its provenance.
export const launchCrop = async ({ src, source, resource, tabId }) => {
  try {
    await chrome.storage.session.set({ [CROP_SRC_KEY]: src, [CROP_META_KEY]: { source: source || '', resource: resource || '' } });
  } catch {
    /* crop page shows a message */
  }
  const url = chrome.runtime.getURL('src/crop/crop.html');
  if (tabId == null) return chrome.tabs.create({ url });
  try {
    // Explicit ready timeout, matching the editor modal's: the watchdog only catches a
    // frame the page's CSP blocked outright, so it must not race the crop page's own
    // load (a short one would close a WORKING modal and re-open the crop page in a tab).
    await chrome.scripting.executeScript({
      target: { tabId }, world: 'ISOLATED', func: mountStencilModal, args: [url, 'Quick crop', 8000, await loadShellTheme()]
    });
  } catch {
    return chrome.tabs.create({ url });
  }
};
