import { filenameFromUrl, getSettings } from '../lib/stencil.js';
import { loadLedger, matchEntries, trackableSource } from '../lib/prefs/ledger.js';
import { loadPins, isPinnedIn, siteOf } from '../lib/prefs/pins.js';
import { scanPageForImages, mergeScanFrames, MAX_IMAGES, BLOCKED_SCHEMES } from '../lib/image/imageScan.js';
import { isEditorTab } from '../lib/menu/editorTabs.js';
import { sourceOf, pinnable } from '../lib/image/imageModel.js';
import { listEl, statusEl, IS_DEVTOOLS } from './panelDom.js';
import { state, rowResource } from './model.js';
import { filterUi, applyFilters } from './filters.js';
import { loadShared, startSharedPolling } from './sharedPins.js';
import { editorMode } from './editorHandle.js';

// A DevTools panel is pinned to the tab it inspects, regardless of focus.
const getTargetTab = async () => {
  if (IS_DEVTOOLS) return chrome.tabs.get(chrome.devtools.inspectedWindow.tabId);
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  return tab;
};

// The tabs this scan reads: on an editor tab, every ticked source page. [] = nothing to scan.
const resolveScanTab = async () => {
  const tab = await getTargetTab();
  const { editorUrl } = await getSettings();
  // Origin matching also matches ordinary pages served beside the editor, so the tab's
  // own bridge confirms before the mode flips.
  const onEditorPage = !!tab && isEditorTab(tab.url || '', editorUrl);
  const editor = onEditorPage && editorMode.available && await editorMode.isLiveEditor(tab.id);
  state.mode = editor ? 'editor' : 'page';
  state.editorTabId = editor ? tab.id : null;
  document.body.classList.toggle('editor-mode', editor);
  // URL-based, so the highlight toggles hide on an editor page even where the probe fails.
  document.body.classList.toggle('on-editor-page', onEditorPage);
  editorMode.setEditorTab(editor ? tab.id : null);
  if (!editor) {
    state.sourceTabId = null;
    return tab ? [tab] : [];
  }
  // A page closed since the picker was filled is dropped; picker order is kept.
  const picked = editorMode.sourceTabs();
  const looked = await Promise.allSettled(picked.map((choice) => chrome.tabs.get(choice.tabId)));
  const live = [];
  for (const r of looked) {
    if (r.status === 'fulfilled') live.push(r.value);
    else editorMode.refresh();
  }
  state.sourceTabId = live.length ? live[0].id : null;
  return live;
};

export const scan = async () => {
  listEl.innerHTML = '';
  statusEl.textContent = 'Scanning…';
  // The format checkboxes render up front so they exist even on an unscannable page.
  state.all = [];
  filterUi.populateFormats(state.all);
  const tabs = await resolveScanTab();
  if (!tabs.length) {
    statusEl.textContent = state.mode === 'editor'
      ? 'Tick a page in “Images from another page” to list its images.'
      : 'This page can’t be scanned.';
    return;
  }
  const scannable = tabs.filter(t => t && !BLOCKED_SCHEMES.some(s => (t.url || '').startsWith(s)));
  if (!scannable.length) {
    statusEl.textContent = 'This page can’t be scanned.';
    return;
  }
  // The FIRST scanned page is the one the highlight / hover controls act on.
  state.activeTabId = scannable[0].id;
  state.activeUrl = scannable[0].url || '';
  await syncHighlightCheckbox(scannable[0].id);
  const images = [];
  const failed = [];
  // Results fold back in `scannable` order so the merged list is stable.
  const scans = await Promise.allSettled(scannable.map((t) => chrome.scripting.executeScript({
    target: { tabId: t.id, allFrames: true }, func: scanPageForImages, args: [MAX_IMAGES]
  })));
  scans.forEach((r, i) => {
    const t = scannable[i];
    if (r.status === 'fulfilled') {
      // Per-row provenance: tab and page URL differ per row once pages are merged.
      for (const it of mergeScanFrames(r.value, MAX_IMAGES))
        images.push({ ...it, sourceTabId: t.id, resource: t.url || '' });
    } else {
      failed.push(`${new URL(t.url || 'http://?').host || 'a page'} (${r.reason.message})`);
    }
  });
  if (!images.length && failed.length) {
    const squeeze = (t) => (t.length <= 40 ? t : t.slice(0, 19) + '…' + t.slice(-19));
    statusEl.textContent = `Could not read ${failed.map(squeeze).join(', ')}.`;
    return;
  }
  if (failed.length) statusEl.textContent = `Couldn’t read ${failed.length} of the ticked pages.`;
  state.all = images.map(it => ({
    ...it,
    // A video is named from its media URL; its still is an opaque data URL.
    name: filenameFromUrl(it.kind === 'video' && it.videoUrl ? it.videoUrl : it.src, it.kind === 'video' ? 'video' : 'image'),
    measured: it.w > 0 && it.h > 0
  }));
  await Promise.all([annotateOpened(), annotatePinned(), loadOpenInSettings(), loadShared()]);
  startSharedPolling();
  filterUi.populateFormats(state.all);
  applyFilters();
};

// Only trackable (http(s)) sources can match another scan.
export const annotateOpened = async () => {
  const { markOpened, openedFirst } = await getSettings();
  state.markOpened = markOpened;
  state.openedFirst = openedFirst;
  document.getElementById('f-mark-opened').checked = markOpened;
  document.getElementById('f-opened-first').checked = openedFirst;
  if (!markOpened) {
    for (const img of state.all) img.opened = [];
    return;
  }
  const ledger = await loadLedger();
  for (const img of state.all) {
    const src = sourceOf(img);
    img.opened = trackableSource(src) ? matchEntries(ledger, src, img.name) : [];
  }
};

// The pin store is keyed by the page's origin.
export const annotatePinned = async () => {
  const { showPinned, hoverHighlight } = await getSettings();
  state.showPinned = showPinned;
  state.hoverHighlight = hoverHighlight;
  document.getElementById('f-show-pinned').checked = showPinned;
  const hh = document.getElementById('f-hover-hl');
  if (hh) hh.checked = hoverHighlight;
  const pins = await loadPins();
  // Per-ROW site, matching the write side: merged rows come from other pages.
  for (const img of state.all)
    img.pinned = pinnable(img) && isPinnedIn(pins, siteOf(rowResource(img)), sourceOf(img));
};

// Cached so the synchronous buildMenu can gate its items without an async read.
export const loadOpenInSettings = async () => {
  const { desktopScheme, telegramBotUsername } = await getSettings();
  state.openIn = { desktopScheme, telegramBotUsername };
};

// The highlight lives on the page and survives the popup closing.
export const syncHighlightCheckbox = async (tabId) => {
  try {
    const [res] = await chrome.scripting.executeScript({
      target: { tabId }, func: () => !!document.getElementById('stencil-hl-style')
    });
    document.getElementById('f-highlight').checked = !!res?.result;
  } catch {
    /* restricted page — leave as-is */
  }
};
