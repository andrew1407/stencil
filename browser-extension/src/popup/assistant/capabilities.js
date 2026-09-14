// ── Injected controller capabilities (llm-contract.md §8) ───────────────────
// What each whitelisted op actually does on this surface: focus the page element, hand
// the image to the editor, attach it for analysis, list/scan another tab, pin, re-scan,
// open a URL, change the panel's own settings, clear the chat. The chat controller is
// built from them at the bottom and kept on `state.controller`.
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, buildHandoff, resumeInOpenEditor } from '../../lib/stencil.js';
import { highlightSourceOnTab } from '../../lib/hoverHighlight.js';
import { highlightColorValue } from '../../lib/highlightColor.js';
import { sourceOf, editableSrc } from '../../lib/imageModel.js';
import { formatOfItem } from '../../lib/filters.js';
import { decodeSize } from '../../lib/rasterize.js';
import { createLlmClient } from '../../llm/llmClient.js';
import { createChatController, translateOpenActions } from '../../llm/chatController.js';
import { openPanelDialog } from '../dialogShell.js';
import { MSG } from '../../lib/messages.js';
import { toLlmImage } from './shared.js';

export const createCapabilities = ({ getItems, getTabId, getPageUrl, openHere, pinEntry,
                                     unpinEntry, rescanPage, setTheme, setFilters, setAccent,
                                     cachedSettings, state }) => {

  // `clearChat`'s in-panel confirm (dialogShell.js — the same yes/no shape as the
  // editor-mode close confirm; click-away, Escape and Cancel all mean no).
  const confirmClearChat = () => openPanelDialog({
    build: (finish) => {
      const title = document.createElement('div');
      title.className = 'dialog-title';
      title.textContent = 'Clear this conversation?';
      const what = document.createElement('div');
      what.className = 'dialog-note';
      what.textContent = 'The assistant asked to clear the chat. The transcript and its history go away.';
      const row = document.createElement('div');
      row.className = 'dialog-actions';
      const cancel = document.createElement('button');
      cancel.textContent = 'Cancel';
      cancel.addEventListener('click', () => finish(false));
      const ok = document.createElement('button');
      ok.className = 'primary';
      ok.textContent = 'Clear';
      ok.addEventListener('click', () => finish(true));
      row.append(cancel, ok);
      return [title, what, row];
    },
  }).then((v) => v === true);

  // `focus`: the existing injected page marking (hoverHighlight.js) — outline + scroll
  // into view; true when found. The target tab is the entry's own provenance
  // (sourceTabId) when it has one — a scanTab'd entry lives on that other tab.
  const focusImage = async (index, entry) => {
    const src = sourceOf(entry) || (entry && entry.src) || '';
    const tabId = (entry && entry.sourceTabId != null) ? entry.sourceTabId : getTabId();
    if (!src || tabId == null) return false;
    const color = await highlightColorValue(await cachedSettings());
    return highlightSourceOnTab(tabId, src, color);
  };

  // `open`: the `#stencil=` editor hand-off, with validated open.actions translated
  // onto launch options. In EDITOR mode the image imports INTO that tab (openHere,
  // contract §8) — except a plan that also asked for a filter/layout: the import
  // message can't carry annotations, so only the fragment path will do.
  const openImage = async (action, entry) => {
    const src = editableSrc(entry);
    if (!src) throw new Error('this entry has no openable image source');
    // §8 open.mode "resume": focus the editor tab ALREADY holding this image instead of
    // handing off a fresh copy. Requested edits need a fresh hand-off, so they win over
    // resume; with no open editor holding it, the classic hand-off carries open:'resume'.
    const resume = action.mode === 'resume' && !(action.actions && action.actions.length);
    const extra = [];
    if (action.mode === 'resume' && !resume) {
      extra.push('The requested edits need a fresh editor hand-off — opened a copy instead of resuming');
    }
    if (resume && await resumeInOpenEditor({ source: sourceOf(entry), name: entry && entry.name })) {
      return extra;
    }
    const { page } = await cachedSettings();
    const dataUrl = await fetchAsDataUrl(src, { pageUrl: (entry && entry.resource) || getPageUrl() });
    let width = entry.w || 0, height = entry.h || 0;
    if (!(width > 0 && height > 0)) {
      // Same two-step decode as the attach path (an SVG can't go through
      // createImageBitmap) — unknown dims only mean the crop translation warns.
      try {
        ({ width, height } = await decodeSize({ dataUrl }));
      } catch { /* dims stay unknown — crop translation will warn */ }
    }
    const { launch, warnings: translated } = translateOpenActions(action.actions, { width, height });
    const warnings = extra.concat(translated);
    if (!launch.layout) {
      const landed = await openHere(entry, {
        incognito: !!action.incognito,
        crop: launch.crop || null,
        page: launch.page ? launch.page.size : '',
      });
      if (landed) return warnings;
    }
    const payload = buildHandoff(entry, {
      dataUrl, page, resource: (entry && entry.resource) || getPageUrl(),
      incognito: !!action.incognito, open: resume ? 'resume' : undefined,
    });
    if (launch.page) payload.page = launch.page;
    if (launch.crop) payload.crop = launch.crop;
    if (launch.layout) payload.layout = launch.layout;
    await openEditorTab(payload);
    return warnings;
  };

  // `attach`: fetch bytes through the extension's host permissions (cross-origin /
  // hotlink-protected sources work like the popup's thumbnails), rasterise + downscale.
  // SVG rasterises to PNG here — the contract accepts png/jpeg/webp/gif only.
  const attachImage = async (index, entry) => {
    const src = editableSrc(entry);
    if (!src) {
      throw new Error(entry && entry.kind === 'video'
        ? 'this video has no captured frame yet — play it on the page, then rescan (or drop the video onto the chat to sample frames)'
        : 'no fetchable image source');
    }
    const dataUrl = await fetchAsDataUrl(src, { pageUrl: (entry && entry.resource) || getPageUrl() });
    return toLlmImage({ dataUrl, width: (entry && entry.w) || 0, height: (entry && entry.h) || 0 });
  };

  // `getTabs`: the user's other open http(s) pages (SW SOURCE_TABS). Best-effort: []
  // on any failure. Gated at the SOURCE on the §8 opt-in, so a caller that forgets
  // the check still gets nothing.
  const getTabs = async () => {
    if (!state.llmSettings || state.llmSettings.shareTabs !== true) return [];
    try {
      const res = await chrome.runtime.sendMessage({ type: MSG.SOURCE_TABS });
      return (res && res.ok && Array.isArray(res.tabs)) ? res.tabs : [];
    } catch {
      return [];
    }
  };

  // `scanTab`: scan another open tab (SW SCAN_TAB) and make its images the working
  // set. Items get the same shaping as popup.js scan(): name, measured dims, per-row
  // provenance (sourceTabId + resource) so focus/open/pin work on them unchanged.
  const scanTab = async (tabEntry) => {
    if (!tabEntry || typeof tabEntry.tabId !== 'number') return { ok: false, error: 'no such tab' };
    const res = await chrome.runtime.sendMessage({ type: MSG.SCAN_TAB, tabId: tabEntry.tabId });
    if (!res || !res.ok) return { ok: false, error: (res && res.error) || 'the scan failed' };
    const items = (res.images || []).map((it) => ({
      ...it,
      sourceTabId: res.tabId,
      resource: res.url || '',
      name: filenameFromUrl(it.kind === 'video' && it.videoUrl ? it.videoUrl : it.src, it.kind === 'video' ? 'video' : 'image'),
      measured: it.w > 0 && it.h > 0,
    }));
    state.workingScan = { tabId: res.tabId, url: res.url || '', title: tabEntry.title || '', items };
    return { ok: true, count: items.length, title: state.workingScan.title };
  };

  // `pin`: popup.js's own pin path when injected (persists + re-sorts the list).
  // Entries from a scanTab'd working set pin by their own site (rowResource).
  const pinImage = async (index, entry) => {
    if (!pinEntry) throw new Error('pinning is not available on this surface');
    if (!entry) throw new Error('no such listing entry');
    await pinEntry(entry);
  };

  // `unpin` (§8): the same path in reverse — local pins only, like the row's Unpin.
  const unpinImage = async (index, entry) => {
    if (!unpinEntry) throw new Error('unpinning is not available on this surface');
    if (!entry) throw new Error('no such listing entry');
    await unpinEntry(entry);
  };

  // `rescan` (§8): refresh the conversation's CURRENT listing — the swapped tab while
  // a scanTab swap is live (the same SW scanner keeps the provenance shaping), else
  // the popup's own scan, which the injected getListing already rides.
  const rescanListing = async () => {
    if (state.workingScan) return scanTab({ tabId: state.workingScan.tabId, title: state.workingScan.title });
    if (!rescanPage) return { ok: false, error: 're-scanning is not available on this surface' };
    await rescanPage();
    return { ok: true, count: (getItems() || []).length };
  };

  // `openUrl`: a USER-GIVEN image URL (executor-guarded) → the same editor hand-off
  // as `open`, on a synthesized entry. Editor mode imports into the current editor
  // tab unless incognito was asked for (that always needs a new incognito editor).
  const openUrlImage = async (action) => {
    const url = action.url;
    const dataUrl = await fetchAsDataUrl(url, { pageUrl: getPageUrl() });
    const entry = { kind: 'img', src: url, name: filenameFromUrl(url, 'image'), w: 0, h: 0 };
    const { page } = await cachedSettings();
    if (!action.incognito) {
      const landed = await openHere(entry, { incognito: false, crop: null, page: '' });
      if (landed) return;
    }
    const payload = buildHandoff(entry, { dataUrl, page, resource: getPageUrl(), incognito: !!action.incognito });
    await openEditorTab(payload);
  };

  state.controller = createChatController({
    getClient: () => createLlmClient({ settings: state.llmSettings }),
    // The working set: the scanTab'd tab's images while a swap is live, else the
    // popup's live scan items (contract §8).
    getListing: () => (state.workingScan ? state.workingScan.items : (getItems() || [])),
    formatOfItem,
    focusImage,
    openImage,
    attachImage,
    pageUrl: () => (state.workingScan ? state.workingScan.url : (getPageUrl() || '')),
    getTabs,
    scanTab,
    pinImage,
    unpinImage,
    rescan: rescanListing,
    openUrlImage,
    // Panel settings (§8): the host wires these to its own controls, so a plan that
    // changes the theme, the accent or the filters goes through the very same path a
    // click does.
    setTheme: setTheme ? async (mode) => setTheme(mode) : undefined,
    setFilters: setFilters ? async (patch) => setFilters(patch) : undefined,
    setAccent: setAccent ? async (action) => setAccent(action) : undefined,
    // §10 clearChat (deferred by the controller to the end of the turn): confirm
    // in-panel; on Yes the wipe waits until this turn has rendered.
    clearChat: async () => {
      const ok = await confirmClearChat();
      if (ok) state.wipeAfterTurn = true;
      return ok;
    },
  });

  return { attachImage, pinImage, unpinImage };
};
