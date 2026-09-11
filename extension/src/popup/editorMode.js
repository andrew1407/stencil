// ── Editor mode: the panel while it stands ON the Stencil editor ─────────────
// Wires the two sections popup.js hands over on an editor tab — editorList.js ("Open
// editors") and sourceTabsList.js ("Images from another page") — to the panel's clock,
// the browser's tab events and editorImport.js. Decisions stay pure in lib/editorTabs.js.
import { MSG } from '../lib/messages.js';
import { pollClock } from '../lib/pollClock.js';
import { createEditorList } from './editorList.js';
import { createSourceTabs } from './sourceTabsList.js';
import { createImportHere } from './editorImport.js';

// Debounce for tab open/close/navigate bursts before the source-page list refreshes.
const TABS_REFRESH_DEBOUNCE_MS = 300;

// One request/response round-trip to the service worker, normalising a missing receiver
// (sleeping worker, reloading extension) to the same `{ok:false,error}` every handler uses.
const ask = async (message) => {
  try {
    return (await chrome.runtime.sendMessage(message)) || { ok: false, error: 'no receiver' };
  } catch (err) {
    return { ok: false, error: err.message || 'no receiver' };
  }
};

// Build the editor surface from popup.js's injected pieces (setStatus, run, dismiss, the
// SHARED ⋯ menu, onSourceTab, imageDataUrl, the floating preview) so this holds none of its state.
// Returns { available, isLiveEditor, setEditorTab, sourceTab, importHere, refresh }.
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
  // The DevTools panel has no editor sections (it is pinned to one tab), so every method
  // no-ops there and popup.js keeps the classic surface.
  const present = !!(listEl && listedEl);
  let editorTabId = null;   // the editor tab this panel stands on (null = page mode)

  const editorList = createEditorList({ listEl, searchEl, regexEl, ask, menu, setStatus,
    dismiss, run, preview, getEditorTabId: () => editorTabId });
  const sources = createSourceTabs({ listedEl, srcFilterEl, srcRegexEl, allBtn, noneBtn,
    noteEl, ask, menu, setStatus, dismiss, onSourceTab });
  const importHere = createImportHere({ ask, setStatus, imageDataUrl, dismiss,
    getEditorTabId: () => (present ? editorTabId : null), refreshEditors: editorList.refresh });

  // ── Live source-page list ───────────────────────────────────────────────────
  // The choices follow the browser: opening, closing, or navigating a tab
  // refreshes the list (debounced — a burst of tab events is one refresh). A
  // TICKED tab that went away also drops its images from the merged list.
  let tabsRefreshTimer = null;
  const refreshChoicesLive = () => {
    if (editorTabId == null) return;   // the section only exists in editor mode
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

  // ── Poll-while-open (previews) ─────────────────────────────────────────────
  // Rides the panel's shared clock (lib/pollClock.js) — the same tick the shared pins use,
  // an MV3 panel being too short-lived for a background channel.
  const poll = () => {
    editorList.refresh();
    // The DevTools panel has no chrome.tabs events — its page list rides the
    // same poll instead (popup/side panel refresh on the events below).
    if (!chrome.tabs?.onCreated) refreshChoicesLive();
  };
  const startPolling = () => pollClock.add(poll);
  const stopPolling = () => pollClock.remove(poll);

  if (present) {
    window.addEventListener('pagehide', stopPolling);
    // Keep the source-page list in step with the browser's tabs. chrome.tabs
    // events exist in the popup and side panel; the DevTools panel lacks the
    // API entirely (optional chaining) and refreshes via the poll instead.
    chrome.tabs?.onCreated?.addListener(refreshChoicesLive);
    chrome.tabs?.onRemoved?.addListener(refreshChoicesLive);
    chrome.tabs?.onUpdated?.addListener((tabId, info) => {
      // Only changes the list can SHOW: a navigation, a retitle, or a load
      // settling — not every favicon/audible/status flicker.
      if (info.url || info.title || info.status === 'complete') refreshChoicesLive();
    });
    searchEl.addEventListener('input', editorList.render);
    regexEl.addEventListener('change', editorList.render);
    refreshBtn.addEventListener('click', () => run(editorList.refresh));
    rescanBtn.addEventListener('click', () => run(async () => {
      await sources.refresh();
      onSourceTab(sources.picked());   // re-scan whatever is still ticked (nothing = clear the list)
    }));
    srcFilterEl?.addEventListener('input', sources.render);
    srcRegexEl?.addEventListener('change', sources.render);
    // Select all applies to what the FILTER shows, so "regex + select all" is one gesture.
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
    // False on a surface without the editor-mode markup (the DevTools panel), so popup.js
    // never flips into a mode this document can't render.
    available: present,

    // Is this tab REALLY a Stencil editor — does its bridge answer? Origin matching alone
    // also matches ordinary pages served beside the editor (popup.js resolveScanTab).
    async isLiveEditor(tabId) {
      if (!present || tabId == null) return false;
      const res = await ask({ type: MSG.EDITOR_STATE, tabId, thumbnail: false });
      return !!(res && res.ok);
    },

    // Enter (or leave) editor mode. Called from every scan, so it stays synchronous:
    // the refreshes it starts are deliberately not awaited.
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

    // The pages whose images the ordinary list shows — [] while none is ticked. popup.js
    // scans each and merges the results, tagging every row with the tab it came from.
    sourceTabs() {
      return sources.picked();
    },

    importHere,

    // Re-pull both lists (the source page went away mid-scan, or a manual refresh).
    refresh() {
      if (!present || editorTabId == null) return;
      run(sources.refresh);
      run(editorList.refresh);
    },
  };
};
