// Shared extension helpers; service-worker-safe (no FileReader / DOM).
import { mountStencilModal } from './overlay.js';
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


export const CROP_SRC_KEY = 'stencil-crop-src';
// Provenance, threaded through so the cropped image keeps where it came from.
export const CROP_META_KEY = 'stencil-crop-meta';

// Falls back to a real tab when the modal cannot be injected or the frame is CSP-blocked.
export const launchCrop = async ({ src, source, resource, tabId }) => {
  try {
    await chrome.storage.session.set({ [CROP_SRC_KEY]: src, [CROP_META_KEY]: { source: source || '', resource: resource || '' } });
  } catch {
    /* crop page shows a message */
  }
  const url = chrome.runtime.getURL('src/crop/crop.html');
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
