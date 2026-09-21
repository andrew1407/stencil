// Editor mode: the panel while it stands on the Stencil editor. Wires editorList.js and
// sourceTabsList.js to the panel's clock and the browser's tab events; decisions stay
// pure in lib/editorTabs.js.
import { MSG } from '../../lib/messages.js';
import { pollClock } from '../../lib/pollClock.js';
import { createEditorList } from './editorList.js';
import { createSourceTabs } from '../list/sourceTabsList.js';
import { createImportHere } from './editorImport.js';

const TABS_REFRESH_DEBOUNCE_MS = 300;

// A missing receiver (sleeping worker, reloading extension) normalises to `{ok:false,error}`.
const ask = async (message) => {
  try {
    return (await chrome.runtime.sendMessage(message)) || { ok: false, error: 'no receiver' };
  } catch (err) {
    return { ok: false, error: err.message || 'no receiver' };
  }
};

export const createEditorMode = ({ setStatus, run, dismiss, menu, onSourceTab, imageDataUrl, preview }) => {
  const listEl = document.getElementById('ed-list');
  const searchEl = document.getElementById('ed-search');
  const regexEl = document.getElementById('ed-regex');
  const refreshBtn = document.getElementById('ed-refresh');
  const listedEl = document.getElementById('src-list');
  const srcFilterEl = document.getElementById('src-filter');
  const srcRegexEl = document.getElementById('src-regex');
  const allBtn = document.getElementById('src-all');
  const noneBtn = document.getElementById('src-none');
  const rescanBtn = document.getElementById('src-rescan');
  const noteEl = document.getElementById('src-note');
  // The DevTools panel has no editor sections, so every method no-ops there.
  const present = !!(listEl && listedEl);
  let editorTabId = null;   // null = page mode

  const editorList = createEditorList({ listEl, searchEl, regexEl, ask, menu, setStatus,
    dismiss, run, preview, getEditorTabId: () => editorTabId });
  const sources = createSourceTabs({ listedEl, srcFilterEl, srcRegexEl, allBtn, noneBtn,
    noteEl, ask, menu, setStatus, dismiss, onSourceTab });
  const importHere = createImportHere({ ask, setStatus, imageDataUrl, dismiss,
    getEditorTabId: () => (present ? editorTabId : null), refreshEditors: editorList.refresh });

  // A ticked tab that went away also drops its images from the merged list.
  let tabsRefreshTimer = null;
  const refreshChoicesLive = () => {
    if (editorTabId == null) return;
    if (tabsRefreshTimer) clearTimeout(tabsRefreshTimer);
    tabsRefreshTimer = setTimeout(() => {
      tabsRefreshTimer = null;
      run(async () => {
        const before = sources.picked().map((c) => c.tabId).join(',');
        await sources.refresh();
        const picked = sources.picked();   // the render pruned dead tabIds
        if (picked.map((c) => c.tabId).join(',') !== before) onSourceTab(picked);
      });
    }, TABS_REFRESH_DEBOUNCE_MS);
  };

  // Rides the panel's shared clock: an MV3 panel is too short-lived for a background channel.
  const poll = () => {
    editorList.refresh();
    // The DevTools panel has no chrome.tabs events, so its page list rides the poll.
    if (!chrome.tabs?.onCreated) refreshChoicesLive();
  };
  const startPolling = () => pollClock.add(poll);
  const stopPolling = () => pollClock.remove(poll);

  if (present) {
    window.addEventListener('pagehide', stopPolling);
    chrome.tabs?.onCreated?.addListener(refreshChoicesLive);
    chrome.tabs?.onRemoved?.addListener(refreshChoicesLive);
    chrome.tabs?.onUpdated?.addListener((tabId, info) => {
      // Only changes the list can show, not every favicon/audible flicker.
      if (info.url || info.title || info.status === 'complete') refreshChoicesLive();
    });
    searchEl.addEventListener('input', editorList.render);
    regexEl.addEventListener('change', editorList.render);
    refreshBtn.addEventListener('click', () => run(editorList.refresh));
    rescanBtn.addEventListener('click', () => run(async () => {
      await sources.refresh();
      onSourceTab(sources.picked());
    }));
    srcFilterEl?.addEventListener('input', sources.render);
    srcRegexEl?.addEventListener('change', sources.render);
    // Select all applies to what the FILTER shows.
    allBtn?.addEventListener('click', () => {
      sources.selectAll();
      onSourceTab(sources.picked());
    });
    noneBtn?.addEventListener('click', () => {
      sources.clearSelection();
      onSourceTab(sources.picked());
    });
  }

  return {
    available: present,

    // Does the tab's bridge answer? Origin matching alone also matches pages served beside the editor.
    async isLiveEditor(tabId) {
      if (!present || tabId == null) return false;
      const res = await ask({ type: MSG.EDITOR_STATE, tabId, thumbnail: false });
      return !!(res && res.ok);
    },

    // Called from every scan, so it stays synchronous: the refreshes are not awaited.
    setEditorTab(tabId) {
      if (!present) return;
      editorTabId = tabId == null ? null : tabId;
      if (editorTabId == null) {
        stopPolling();
        editorList.clear();
        return;
      }
      run(sources.refresh);
      run(editorList.refresh);
      startPolling();
    },

    sourceTabs() {
      return sources.picked();
    },

    importHere,

    refresh() {
      if (!present || editorTabId == null) return;
      run(sources.refresh);
      run(editorList.refresh);
    },
  };
};
