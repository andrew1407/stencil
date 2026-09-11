// ── Scanning the target page(s) and annotating what comes back. ──────────────
import { filenameFromUrl, getSettings } from '../lib/stencil.js';
import { loadLedger, matchEntries, trackableSource } from '../lib/ledger.js';
import { loadPins, isPinnedIn, siteOf } from '../lib/pins.js';
import { scanPageForImages, mergeScanFrames, MAX_IMAGES, BLOCKED_SCHEMES } from '../lib/imageScan.js';
import { isEditorTab } from '../lib/editorTabs.js';
import { sourceOf, pinnable } from '../lib/imageModel.js';
import { listEl, statusEl, IS_DEVTOOLS } from './panelDom.js';
import { state, rowResource } from './model.js';
import { filterUi, applyFilters } from './filters.js';
import { loadShared, startSharedPolling } from './sharedPins.js';
import { editorMode } from './editorHandle.js';

// The tab to scan/act on. Popup and side panel ride the active tab of the current
// window; a DevTools panel is pinned to the tab it's inspecting, regardless of focus.
const getTargetTab = async () => {
  if (IS_DEVTOOLS) return chrome.tabs.get(chrome.devtools.inspectedWindow.tabId);
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  return tab;
};

// The tab this scan reads + the mode with it: on an EDITOR tab the page has no images worth
// listing, so the scan follows the picked page. null = nothing to scan, NOT "unscannable".
const resolveScanTab = async () => {
  const tab = await getTargetTab();
  const { editorUrl } = await getSettings();
  // Origin matching is only the cheap PRE-filter — it also matches ordinary pages served beside
  // the editor — so confirm with the tab's own bridge before flipping (editorMode.isLiveEditor).
  const onEditorPage = !!tab && isEditorTab(tab.url || '', editorUrl);
  const editor = onEditorPage && editorMode.available && await editorMode.isLiveEditor(tab.id);
  state.mode = editor ? 'editor' : 'page';
  state.editorTabId = editor ? tab.id : null;
  document.body.classList.toggle('editor-mode', editor);
  // URL-based, probe-independent: the highlight toggles hide on any editor page, including
  // surfaces that never flip to editor mode (DevTools) or where the bridge probe fails.
  document.body.classList.toggle('on-editor-page', onEditorPage);
  editorMode.setEditorTab(editor ? tab.id : null);
  if (!editor) {
    state.sourceTabId = null;
    return tab ? [tab] : [];
  }
  // Editor mode scans EVERY ticked page and merges them, so this returns a list. A page
  // closed since the picker was filled is dropped rather than scanned as a dead tab id.
  // The lookups are independent, so fan out in parallel (picker order is kept).
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

// ── Scan ──
export const scan = async () => {
  listEl.innerHTML = '';
  statusEl.textContent = 'Scanning…';
  // Render the format checkboxes up front (common formats), so they're always
  // visible even on a page that can't be scanned; refreshed once results arrive.
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
  // The FIRST scanned page is the one the highlight / hover controls act on; every row also
  // carries its own tab, so a row from the second page still highlights on the right one.
  state.activeTabId = scannable[0].id;
  state.activeUrl = scannable[0].url || '';
  await syncHighlightCheckbox(scannable[0].id);
  const images = [];
  const failed = [];
  // Scan every frame of every page (content is often in an iframe), then dedupe across
  // frames. The pages are independent, so fan out in parallel; results are folded back
  // in `scannable` order so the merged list is stable.
  const scans = await Promise.allSettled(scannable.map((t) => chrome.scripting.executeScript({
    target: { tabId: t.id, allFrames: true }, func: scanPageForImages, args: [MAX_IMAGES]
  })));
  scans.forEach((r, i) => {
    const t = scannable[i];
    if (r.status === 'fulfilled') {
      // Provenance per row: which tab it came from (hover-highlight, focus) and which page
      // URL to record as the `resource` of a hand-off — both differ per row once several
      // pages are merged into one list.
      for (const it of mergeScanFrames(r.value, MAX_IMAGES))
        images.push({ ...it, sourceTabId: t.id, resource: t.url || '' });
    } else {
      failed.push(`${new URL(t.url || 'http://?').host || 'a page'} (${r.reason.message})`);
    }
  });
  if (!images.length && failed.length) {
    // Long source names would stretch the line into a wall — squeeze each token.
    const squeeze = (t) => (t.length <= 40 ? t : t.slice(0, 19) + '…' + t.slice(-19));
    statusEl.textContent = `Could not read ${failed.map(squeeze).join(', ')}.`;
    return;
  }
  if (failed.length) statusEl.textContent = `Couldn’t read ${failed.length} of the ticked pages.`;
  state.all = images.map(it => ({
    ...it,
    // Name a video from its media URL (the still is an opaque data URL); fall back
    // to a generic name when the video is an in-page blob with no usable URL.
    name: filenameFromUrl(it.kind === 'video' && it.videoUrl ? it.videoUrl : it.src, it.kind === 'video' ? 'video' : 'image'),
    measured: it.w > 0 && it.h > 0
  }));
  // Four independent reads over disjoint state — together, so a scan is one hop, not four.
  await Promise.all([annotateOpened(), annotatePinned(), loadOpenInSettings(), loadShared()]);
  startSharedPolling();
  filterUi.populateFormats(state.all);
  applyFilters();
};

// Tag each image with the ledger entries that show it's already been opened in an
// editor (drives the yellow badge + the resume chooser). Gated by the markOpened
// setting; only trackable (http(s)) sources can match another scan.
export const annotateOpened = async () => {
  const { markOpened, openedFirst } = await getSettings();
  state.markOpened = markOpened;
  state.openedFirst = openedFirst;
  // Keep the popup toggles in sync with the persisted settings (and the options page).
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

// Tag each image with whether it's pinned on this site (drives the gray outline, the
// pin button's active state, and the float-to-top sort). The pin store is keyed by the
// page's origin so a pin made here matches the same image on the same site next visit.
export const annotatePinned = async () => {
  const { showPinned, hoverHighlight } = await getSettings();
  state.showPinned = showPinned;
  state.hoverHighlight = hoverHighlight;
  document.getElementById('f-show-pinned').checked = showPinned;
  const hh = document.getElementById('f-hover-hl');
  if (hh) hh.checked = hoverHighlight;
  const pins = await loadPins();
  // Per-ROW site (matching the write side): an editor-mode scan merges rows from
  // other pages, so keying on state.activeUrl never matched their pins.
  for (const img of state.all)
    img.pinned = pinnable(img) && isPinnedIn(pins, siteOf(rowResource(img)), sourceOf(img));
};

// Cache the "Open in…" operator config (desktop URL scheme + Telegram bot username) so the
// synchronous buildMenu can gate its items without an async read. Refreshed on scan and
// when the options page changes them (storage.onChanged, below).
export const loadOpenInSettings = async () => {
  const { desktopScheme, telegramBotUsername } = await getSettings();
  state.openIn = { desktopScheme, telegramBotUsername };
};

// The highlight lives on the page (survives the popup closing), so on open reflect
// its real state in the checkbox rather than defaulting to unchecked.
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
