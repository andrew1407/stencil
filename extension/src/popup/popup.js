// ── Popup: list, filter, and act on every image on the active page ───────────
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchEditorModal, launchCrop, getSettings, setSettings, blobToDataUrl, buildHandoff, resumeInOpenEditor } from '../lib/stencil.js';
import { LEDGER_KEY, loadLedger, matchEntries, trackableSource } from '../lib/ledger.js';
import { PINS_KEY, loadPins, isPinnedIn, siteOf, setPinned, projectNameColor } from '../lib/pins.js';
import { highlightColorValue } from '../lib/highlightColor.js';
import { scanPageForImages, mergeScanFrames, MAX_IMAGES, BLOCKED_SCHEMES } from '../lib/imageScan.js';
import { toggleStencilHighlight } from '../lib/highlight.js';
import { highlightSourceOnTab } from '../lib/hoverHighlight.js';
import { icon } from '../lib/icons.js';
import { shortName } from '../lib/displayName.js';
import { passesFilters } from '../lib/filters.js';
import {
  CONNECTIONS_KEY, loadConnections, collectSharedPins, connectionByUrl,
  createProject, fetchProjectImage, pinTargetMode, projectRequestFromImage,
} from '../lib/connections.js';
import { sourceOf, posterImage, editableSrc, pinnable, sharedMatchesSearch, hostLabel } from '../lib/imageModel.js';
import { isEditorTab } from '../lib/editorTabs.js';
import { extractDraggedUrl, guessKindFromUrl } from '../lib/dragUrl.js';
import { MSG } from '../lib/messages.js';
import { buildStencilSchemeUrl, encodeTelegramStartPayload, buildTelegramLink, INLINE_WARN_CHARS, INLINE_MAX_CHARS } from '../lib/openIn.js';
import { URL_DRAG_TYPES } from '../lib/chatDrop.js';
import {
  entryFromUrl, entryFromDrop, dragActionAllowed, sameSource, dragPayloadKind,
} from '../lib/dropEntry.js';
import { openPanelDialog } from './dialogShell.js';
import { rasterizeToPngDataUrl, isSvgType, isSvgUrl, mediaTypeOf } from '../lib/rasterize.js';
import { createDragSectionOpener, ASSISTANT_SECTION, SEARCH_SECTION, SPRING_DWELL_MS } from '../lib/dragSections.js';
import { createSectionPeek, peekPosition, isTypingTarget } from '../lib/sectionPeek.js';
import { loadLlmSettings, assistantEnabled, LLM_SETTINGS_KEY } from '../llm/llmSettings.js';
import { createAssistant, applyAssistantVisibility } from './assistant.js';
import { createEditorMode } from './editorMode.js';
import { watchNumericInputs } from '../lib/numericInput.js';
import { observeReveal, flashLanding, filterLeave, createFilterTransition } from '../lib/motion.js';
import { createActionMenu } from '../lib/actionMenu.js';
import { createHoverPreview } from '../lib/hoverPreview.js';
import { createFilterUi, FILTERS_KEY } from '../lib/filterUi.js';
import { createCollapsibleSections } from '../lib/collapsibleSections.js';
import { createLogoDragMenu } from '../lib/logoDragMenu.js';
import { rowTitle, thumbInitialSrc, dimText, rowBadges, rowOutlineClass } from '../lib/rowModel.js';
import { initTooltips } from '../lib/controlTooltip.js';
import { enhanceSelect } from '../lib/customSelect.js';

const listEl = document.getElementById('list');
// Rows fade + lift through the list as it scrolls, and each rebuild's rows are
// picked up by the observer itself — applyFilters() stays untouched.
observeReveal(listEl, '.row');
const statusEl = document.getElementById('status');
// Clearing the status line is the one "toast disappearing" moment this surface has.
// `.status:empty` is display:none and display can't transition, so the fade runs
// FIRST and the text is emptied when it lands. A new message mid-fade cancels it.
const STATUS_LEAVE_MS = 200;
let statusLeaveTimer = null;
const clearStatus = () => {
  clearTimeout(statusLeaveTimer);
  if (!statusEl.textContent) return;         // already empty — nothing to play out
  statusEl.classList.add('status-leaving');
  statusLeaveTimer = setTimeout(() => {
    statusEl.classList.remove('status-leaving');
    statusEl.textContent = '';
  }, STATUS_LEAVE_MS);
};
// Any new message cancels a pending fade, so it never shows up already half-gone.
const observeStatusWrites = new MutationObserver(() => {
  if (statusEl.textContent && statusEl.classList.contains('status-leaving')) {
    clearTimeout(statusLeaveTimer);
    statusEl.classList.remove('status-leaving');
  }
});
observeStatusWrites.observe(statusEl, { childList: true, characterData: true, subtree: true });
const countEl = document.getElementById('count');
const previewEl = document.getElementById('preview');
const previewImg = previewEl.querySelector('img');
const menuEl = document.getElementById('action-menu');

const THUMB_PX = 48;     // rendered thumbnail size (see .thumb in popup.css)
// Placeholder thumbnail for a video whose frame couldn't be read (cross-origin).
const PLAY_THUMB = 'data:image/svg+xml,' + encodeURIComponent(
  '<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48"><rect width="48" height="48" fill="#2b2f3a"/><polygon points="19,15 35,24 19,33" fill="#7c3aed"/></svg>');

// `mode`: 'page' (classic — scan the tab we're on) or 'editor' (standing ON the Stencil
// editor: the editor sections show, the scan follows `sourceTabId`, rows import into it).
const state = { all: [], filtered: [], mode: 'page', sourceTabId: null, editorTabId: null, activeTabId: null, activeUrl: '', markOpened: true, openedFirst: true, showPinned: true, hoverHighlight: false, connections: [], shared: [], openIn: { desktopScheme: 'stencil', telegramBotUsername: '' } };

// How often (ms) the popup re-pulls shared pins from connected servers while it's
// open. MV3 popups are short-lived, so a light poll-while-open is both simple and
// correct — no background WS to keep alive. Cleared when the surface unloads.
const SHARED_POLL_MS = 8000;

// This controller drives three surfaces — the toolbar popup, the docked side panel, and
// the DevTools panel — told apart by the host document's path. The docked ones persist
// and re-scan; only the popup closes after an action.
const IS_SIDE_PANEL = location.pathname.includes('sidepanel');
const IS_DEVTOOLS = location.pathname.includes('devtools');
// The popup is the only ephemeral surface; the docked ones persist, so leave them.
const dismiss = () => { if (!IS_SIDE_PANEL && !IS_DEVTOOLS) window.close(); };

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

// The tab the user is LOOKING at, which in editor mode is NOT the tab being scanned. Anything
// that mounts UI on a page (crop modal, editor modal, drop-zone overlay) must target this one
// or it lands on a background tab; reads of the scanned CONTENT keep `state.activeTabId`.
const surfaceTabId = () => (state.mode === 'editor' ? state.editorTabId : state.activeTabId);

// Provenance/pin/search predicates (sourceOf / posterImage / editableSrc / pinnable /
// sharedMatchesSearch) live in ../lib/imageModel.js — pure + unit-tested.

// Measure unknown-size images only once their row scrolls into view (thumbnails
// use loading="lazy" too). All matching rows render up front so filtering shows all.
const measureObs = new IntersectionObserver((entries) => {
  for (const e of entries) {
    if (!e.isIntersecting) continue;
    measureObs.unobserve(e.target);
    measure(e.target._image, e.target._dimEl, e.target);
  }
}, { root: listEl, rootMargin: '200px' });

// A filter change animates BOTH ways (lib/motion.js): rows the filters no longer admit
// fade out where they stood while the arriving ones ramp in. Rows are keyed by source,
// so a re-render of the same set animates nothing. A ghost is on its way out and belongs
// to no image, so it must not be measured any more.
const filterTransition = createFilterTransition({
  list: listEl,
  onLeave: (li) => measureObs.unobserve(li),
});

// Stable identity for a row across renders — what the transition diffs. The source alone
// won't do: a server row and a local pin of the same image are different rows, and so are
// a video's poster and a plain image that happen to share a URL (each has its own toggle).
const rowKey = (image) => (image.shared
  ? `server:${image.serverUrl}:${image.projectId || ''}:`
  : `page:${image.kind || ''}:${image.poster ? 'p' : ''}${image.meta ? 'm' : ''}:`)
  + (sourceOf(image) || image.src || image.name || '');
// The rendered row for an image, found by that key (indexes shift while ghosts play out).
const rowElFor = (image) => {
  try { return listEl.querySelector(`li[data-key="${CSS.escape(rowKey(image))}"] .row`); }
  catch { return null; }
};

// ── Scan ──
const scan = async () => {
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
  await annotateOpened();
  await annotatePinned();
  await loadOpenInSettings();
  await loadShared();
  startSharedPolling();
  filterUi.populateFormats(state.all);
  applyFilters();
};

// Tag each image with the ledger entries that show it's already been opened in an
// editor (drives the yellow badge + the resume chooser). Gated by the markOpened
// setting; only trackable (http(s)) sources can match another scan.
const annotateOpened = async () => {
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
const annotatePinned = async () => {
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

const isPinned = (image) => state.showPinned && !!image.pinned;

// The page a row came from. Editor mode merges several pages into one list, so each row
// remembers its own; everywhere else that is just the scanned page.
const rowResource = (image) => (image && image.resource) || state.activeUrl;

// Cache the "Open in…" operator config (desktop URL scheme + Telegram bot username) so the
// synchronous buildMenu can gate its items without an async read. Refreshed on scan and
// when the options page changes them (storage.onChanged, below).
const loadOpenInSettings = async () => {
  const { desktopScheme, telegramBotUsername } = await getSettings();
  state.openIn = { desktopScheme, telegramBotUsername };
};

// ── Shared pins (connected collaboration servers) ───────────────────────────
// A server project renders alongside the page's own images with a golden outline; its
// thumbnail / editor hand-off are fetched over the server's Bearer-authed endpoint.
const SHARED_POLL = { timer: null };

// Map a shared-pin record (connections.js) to the popup's image shape. No `src` (the
// download is authed — a bare <img> can't load it); the bytes come via fetchProjectImage,
// and measured:true keeps the size observer off a row with no probe-able URL.
const sharedToImage = (pin) => ({
  kind: 'img',
  src: '',
  name: pin.name,
  w: 0,
  h: 0,
  measured: true,
  shared: true,
  serverUrl: pin.serverUrl,
  projectId: pin.projectId,
  source: pin.source,
  resource: pin.resource || '',
  // Project's custom accent colour ("#rrggbb", or "" = default) for painting the row name.
  color: pin.color || '',
  opened: [],
  pinned: false,
});

// The connection that owns a shared row (by its server origin), or null.
const sharedConn = (image) => connectionByUrl(state.connections, image.serverUrl);

// Thumbnail bytes (authed) as a data URL: the edited `result`, falling back to the
// `original`. Cached on the row. Distinct from the editor hand-off (sharedDataUrl),
// which needs the untouched original so the editor can re-apply the saved filter/lines.
const sharedThumbUrl = async (image) => {
  if (image._thumbUrl) return image._thumbUrl;
  const conn = sharedConn(image);
  if (!conn) throw new Error('no connection for shared pin');
  let blob = null;
  try { blob = await fetchProjectImage(conn, image.projectId, 'result'); }
  catch { blob = await fetchProjectImage(conn, image.projectId, 'original'); }
  const dataUrl = await blobToDataUrl(blob);
  image._thumbUrl = dataUrl;
  return dataUrl;
};

// Original (unedited) bytes as a data URL (authed) for the editor / crop hand-off, so the
// editor re-opens the raw image and re-applies the saved filter + lines. Cached on the row
// and in the preview cache so the open/crop paths share one fetch.
const sharedDataUrl = async (image) => {
  if (image._dataUrl) return image._dataUrl;
  const conn = sharedConn(image);
  if (!conn) throw new Error('no connection for shared pin');
  const dataUrl = await blobToDataUrl(await fetchProjectImage(conn, image.projectId, 'original'));
  image._dataUrl = dataUrl;
  if (image.source) previewCache.set(image.source, dataUrl);
  return dataUrl;
};

// Set a shared row's thumbnail from its authed EDITED-result data URL; hide the <img> if
// the fetch fails (unreachable server, or a project with no stored bytes yet).
const resolveSharedThumb = async (image, thumb) => {
  try {
    thumb.src = await sharedThumbUrl(image);
    thumb.style.visibility = 'visible';
  } catch {
    thumb.style.visibility = 'hidden';
  }
};

// Pull the current shared pins from every connection into state.shared. Best-effort:
// an unreachable server is skipped (collectSharedPins swallows its error).
const loadShared = async () => {
  state.connections = await loadConnections();
  const pins = await collectSharedPins(state.connections);
  // Preserve any already-resolved bytes across a refresh (match on server+project): both
  // the edited-result thumbnail and the original editor hand-off, so neither re-fetches.
  const prev = new Map(state.shared.map((s) => [`${s.serverUrl}\n${s.projectId}`, s]));
  state.shared = pins.map((p) => {
    const row = sharedToImage(p);
    const old = prev.get(`${row.serverUrl}\n${row.projectId}`);
    row._dataUrl = old ? old._dataUrl : null;
    row._thumbUrl = old ? old._thumbUrl : null;
    return row;
  });
  // The set of ORIGINAL source URLs that exist on a server, so a LOCAL pin of the same
  // image also shows the golden "on a server" outline (not just the separate shared rows).
  state.sharedSources = new Set(pins.map((p) => p.origin).filter(Boolean));
  // origin URL -> Set(serverUrl) + the connected hosts, for the "server pins" filter.
  state.serverByOrigin = new Map();
  for (const p of pins) {
    if (!p.origin) continue;
    if (!state.serverByOrigin.has(p.origin)) state.serverByOrigin.set(p.origin, new Set());
    state.serverByOrigin.get(p.origin).add(p.serverUrl);
  }
  state.serverHosts = state.connections.map((c) => c.url);
  syncServerFilterUI();
};

// Show/populate the popup's "server pins" checkbox + per-server select (only when at
// least one server is connected). Mirrors the same filter on the options page.
const syncServerFilterUI = () => {
  const has = (state.serverHosts || []).length > 0;
  const wrap = document.getElementById('f-server-pins-wrap');
  const showServer = document.getElementById('f-server-pins');
  const sel = document.getElementById('f-server-store');
  if (wrap) wrap.hidden = !has;
  if (sel) {
    sel.hidden = !has || !(showServer && showServer.checked);
    if (has) {
      const prev = sel.value || 'all';
      sel.innerHTML = '<option value="all">Any server</option>'
        + state.serverHosts.map((u) => `<option value="${u}">${hostLabel(u)}</option>`).join('');
      sel.value = [...sel.options].some((o) => o.value === prev) ? prev : 'all';
    }
  }
};

// Poll-while-open: refresh shared pins on a light interval so server-side changes show
// up without a manual rescan. Only runs when at least one server is connected.
const startSharedPolling = () => {
  if (SHARED_POLL.timer || !state.connections.length) return;
  SHARED_POLL.timer = setInterval(async () => {
    await loadShared();
    applyFilters();
  }, SHARED_POLL_MS);
};
const stopSharedPolling = () => {
  if (SHARED_POLL.timer) clearInterval(SHARED_POLL.timer);
  SHARED_POLL.timer = null;
};
window.addEventListener('pagehide', stopSharedPolling);

// Format pills, reading the controls, and filter persistence live in lib/filterUi.js;
// onChange routes a pill toggle through the same applyFilters pass a click takes.
const filterUi = createFilterUi({ doc: document, onChange: () => applyFilters() });

// ── Filtering ──
let filters = {};
const renderCount = () => {
  const total = state.all.length + state.shared.length;
  countEl.textContent = total ? `(${state.filtered.length}/${total})` : '';
};


const applyFilters = () => {
  filters = filterUi.read();
  filterUi.save();                       // persist the current filter state on every change
  state.filtered = state.all.filter(it => passesFilters(it, filters));
  // Float pinned images (primary) then already-opened images (secondary) to the top.
  // Array.sort is stable, so images keep their scan order within each group, and each
  // key is a no-op when its toggle is off — preserving the page's natural order.
  const rank = (it) => (isPinned(it) ? 2 : 0) + (state.openedFirst && isOpened(it) ? 1 : 0);
  if (state.showPinned || state.openedFirst)
    state.filtered.sort((a, b) => rank(b) - rank(a));
  // Server-pins filter: the checkbox shows/hides server-stored items (the golden cue on
  // local pins, gated below in renderRow, + the shared rows here); the select narrows the
  // shared rows to one connected server.
  const showServerEl = document.getElementById('f-server-pins');
  const storeSel = document.getElementById('f-server-store');
  state.showServerPins = !showServerEl || showServerEl.checked;
  const store = (storeSel && !storeSel.hidden) ? storeSel.value : 'all';
  // Shared (server) pins list after the page's own images, newest-first.
  let sharedRows = state.shared.filter(s => sharedMatchesSearch(s, filters.search, filters.regex));
  if (!state.showServerPins) sharedRows = [];
  else if (store !== 'all') sharedRows = sharedRows.filter(s => s.serverUrl === store);
  state.filtered = state.filtered.concat(sharedRows);
  // Rows this pass drops — a narrowed search, a format pill off, an unpin with "show
  // pinned" off — fade out where they stood, and the ones it admits ramp in. The list is
  // rebuilt wholesale, so the transition diffs the outgoing render against the new set;
  // a filter fade is deliberately lighter than the destructive leave a delete plays.
  filterTransition.begin();
  listEl.innerHTML = '';
  renderCount();
  if (!state.all.length && !sharedRows.length) {
    listEl.innerHTML = '<li class="empty">No images found on this page.</li>';
  } else if (!state.filtered.length) {
    listEl.innerHTML = '<li class="empty">No images match the filters.</li>';
  } else {
    // `.status:empty` is display:none, which can't transition — so fade it out first,
    // then empty it (lib/animations.css .status-leaving).
    clearStatus();
    // Render every matching row; thumbnails + size measurement load lazily on scroll.
    state.filtered.forEach(renderRow);
  }
  filterTransition.end();
};

// A row that represents a project (a shared server-project row, or a local pin with kind
// 'project') — only these recolour their name; plain page images/pins keep the theme colour.
const isProjectRow = (image) => !!image.shared || image.kind === 'project';

// ── Rows ──
const renderRow = (image) => {
  const li = document.createElement('li');
  li.dataset.key = rowKey(image);   // the filter transition diffs renders by this
  const row = document.createElement('div');
  row.className = 'row';

  const thumb = document.createElement('img');
  thumb.className = 'thumb';
  // What the row shows (tooltip / thumb source / badges / outline) is lib/rowModel.js.
  const initSrc = thumbInitialSrc(image, PLAY_THUMB);
  if (initSrc) thumb.src = initSrc;
  thumb.loading = 'lazy';
  // No native title here: the floating hover preview would cover it (the info stays on the
  // name). A broken thumb is likely hotlink-protected — retry once via fetchAsDataUrl
  // (host permissions), reusing the preview cache; a frameless video gets the play glyph.
  thumb.addEventListener('error', async () => {
    if (image.kind === 'video') { thumb.src = PLAY_THUMB; return; }
    const src = editableSrc(image);
    if (thumb.dataset.recovered || !src || src.startsWith('data:')) { thumb.style.visibility = 'hidden'; return; }
    thumb.dataset.recovered = '1';
    try {
      const dataUrl = previewCache.get(src) || await fetchAsDataUrl(src, { pageUrl: rowResource(image) });
      previewCache.set(src, dataUrl);
      thumb.src = dataUrl;
    } catch {
      thumb.style.visibility = 'hidden';
    }
  });
  bindPreview(thumb, image);
  // Shared (server) rows have no plain src — their download is Bearer-authed, so resolve
  // the thumbnail through the owning connection instead of letting a bare <img> 404.
  if (image.shared) resolveSharedThumb(image, thumb);

  const meta = document.createElement('div');
  meta.className = 'meta';
  const name = document.createElement('div');
  name.className = 'name clickable';
  name.textContent = image.name;
  name.title = rowTitle(image);
  // Project rows paint the name in the project's custom `color`, or a fixed neutral grey when
  // unset. Values are inlined (not a CSS var) so a stale-cached theme.css can't blank the name.
  if (isProjectRow(image)) {
    name.style.color = projectNameColor(image.color, '#80868f');
    name.style.textShadow = '0 1px 2px rgba(0,0,0,0.55)';
  }

  // Click → open in editor; double-click → quick crop (videos act on their frame).
  bindRowGestures(thumb, image);
  bindRowGestures(name, image);
  const sub = document.createElement('div');
  sub.className = 'sub';
  const badgeSpan = (b) => {
    const span = document.createElement('span');
    span.className = b.cls;
    if (b.html) span.innerHTML = b.html;   // a fixed icon from lib/icons.js, never page data
    else span.textContent = b.text;
    if (b.title) span.title = b.title;
    return span;
  };
  const opened = isOpened(image);
  const badges = rowBadges(image, { opened });
  const openedBadge = opened ? badges.pop() : null;   // the opened flag renders after the size
  for (const b of badges) sub.appendChild(badgeSpan(b));
  const dim = document.createElement('span');
  dim.className = 'dim';
  dim.textContent = dimText(image);
  sub.appendChild(dim);
  if (openedBadge) sub.appendChild(badgeSpan(openedBadge));
  // Outline colour: GOLD = on a server, GRAY = local pin, plus the opened yellow.
  const outline = rowOutlineClass(image, {
    showServerPins: state.showServerPins, sharedSources: state.sharedSources, pinned: isPinned(image),
  });
  if (outline) row.classList.add(outline);
  // Already opened in an editor: clicking the row opens the resume/copy chooser
  // (see bindRowGestures / buildMenu).
  if (opened) row.classList.add('opened');
  meta.append(name, sub);

  // Pin toggle (shown only when the item has an openable source). Reflects the raw
  // pinned state so you can unpin even with "show pinned" off (which hides the outline).
  let pinBtn = null;
  if (pinnable(image)) {
    pinBtn = document.createElement('button');
    pinBtn.className = 'pin-btn' + (image.pinned ? ' active' : '');
    pinBtn.innerHTML = icon('pin', { size: 15 });
    pinBtn.title = image.pinned ? 'Unpin' : 'Pin to top';
    pinBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      // Unpin directly; when pinning, offer the local / on-server picker (servers
      // connected) so you can store it remotely without opening the ⋯ menu.
      if (image.pinned || pinTargetMode(state.connections) === 'none') togglePin(image);
      else pinWithPrompt(image, pinBtn);
    });
  }

  const more = document.createElement('button');
  more.className = 'more-btn';
  more.textContent = '⋯';
  more.title = 'Actions';
  more.addEventListener('click', (e) => {
    e.stopPropagation();
    openMenu(more, image);
  });
  // Right-click anywhere on the row opens the same actions menu, at the cursor.
  row.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    openMenuAt(image, e.clientX, e.clientY);
  });

  row.append(thumb, meta, ...(pinBtn ? [pinBtn] : []), more);
  bindHoverHighlight(row, image);
  bindRowDrag(row, image);
  li.appendChild(row);
  listEl.appendChild(li);

  // Defer measuring unknown-size images (most CSS backgrounds) until the row
  // scrolls into view; the observer then measures and re-checks the size filter.
  if (!image.measured) {
    li._image = image;
    li._dimEl = dim;
    measureObs.observe(li);
  }
};

const measure = (image, dimEl, li) => {
  const probe = new Image();
  probe.onload = () => {
    image.w = probe.naturalWidth;
    image.h = probe.naturalHeight;
    image.measured = true;
    dimEl.textContent = dimText(image);
    // The now-known size may no longer match — drop the row, keep the counter synced.
    if (!passesFilters(image, filters)) {
      // The measurement disqualified it — a FILTER dropping the row, not a delete, so it
      // gets the light fade rather than the destructive scatter.
      filterLeave(li, () => {
        li.remove();
        state.filtered = state.filtered.filter(it => it !== image);
        renderCount();
      });
    }
  };
  probe.src = image.src;
};

// ── Floating "…" action menu ──
const run = async (fn) => {
  try {
    await fn();
  } catch (err) {
    statusEl.textContent = `Failed: ${err.message}`;
  }
};

// One shared controller (lib/actionMenu.js) behind the row ⋯ / right-click menus, the
// logo's drag menu placement, and editor mode's per-row menu: the item/sep/label/submenu
// builders, flip-and-clamp placement, and the open/close + Escape machinery.
const menu = createActionMenu({ menuEl, run });
const { item, sep, label, submenu } = menu;
const closeMenu = menu.close;
const placeMenu = menu.place;

// Fill the menu with the actions for `image`. Editor actions come in pairs: a new tab
// and an in-page modal (▣, mirrors the quick-crop modal), each normal and incognito.
const buildMenu = (image) => {
  menuEl.innerHTML = '';
  // Nested actions Open ▸ / Pin ▸ (plus the flat Crop action). Reused across image, shared,
  // video-frame and poster contexts.
  const editSub = (img) => submenu(icon('pencil', { size: 15 }), 'Open', [
    // "Here" means the editor that is already in front of you: the in-page modal on an
    // ordinary page, the editor tab itself in editor mode (openHere).
    item(icon('monitor', { size: 15 }), state.mode === 'editor' ? 'Into this editor' : 'Here', () => openHere(img, false, undefined, pinAnchor)),
    item(icon('external', { size: 15 }), 'In editor', () => sendToEditor(img, false)),
    item(icon('incognito', { size: 15 }), 'In editor (incognito)', () => sendToEditor(img, true)),
  ]);
  // Crop is a single flat action — open the in-page quick-crop modal ("here"). No submenu,
  // no "in editor" variants.
  const cropItem = (img) => item(icon('crop', { size: 15 }), 'Crop', () => openCrop(img));
  // The dialog anchors to the ⋯ button the menu opened from (the anchor is set BEFORE
  // buildMenu runs, so it is captured at build time; a right-click-opened menu has no
  // button and falls back to the centred dialog).
  const pinAnchor = menu.anchor();
  const pinSub = (img) => submenu(icon('pin', { size: 15 }), img.pinned ? 'Pinned' : 'Pin',
    img.pinned
      ? [item(icon('pin', { size: 15 }), 'Unpin', () => togglePin(img)),
         item(icon('server', { size: 15 }), 'Store on server…', () => pinWithPrompt(img, pinAnchor))]
      : [item(icon('pin', { size: 15 }), 'Locally', () => togglePin(img)),
         item(icon('server', { size: 15 }), 'On server…', () => pinWithPrompt(img, pinAnchor))]);
  // "Open in…" hand-off to another front-end. Desktop app: shown whenever a scheme is
  // configured. Telegram bot: ONLY for a shared (server) row with a bot username — a t.me
  // start payload can't carry image bytes. Returns [] when neither applies (submenu omitted).
  const openInFlat = (img) => {
    const oi = state.openIn || {};
    const children = [];
    if (oi.desktopScheme)
      children.push(item(icon('monitor', { size: 15 }), 'Desktop app', () => openInDesktop(img)));
    if (oi.telegramBotUsername && img.shared && img.serverUrl && img.projectId)
      children.push(item(icon('external', { size: 15 }), 'Telegram bot', () => openInTelegram(img)));
    return children.length ? [submenu(icon('external', { size: 15 }), 'Open in…', children)] : [];
  };

  // Already opened: offer to resume the existing editor (switches to the matching
  // project, or lets the user pick when several share this image) or add a fresh
  // numbered copy. Shown first since it's the point of the yellow badge.
  if (isOpened(image)) {
    const n = image.opened.reduce((a, e) => Math.max(a, e.count || 1), 0);
    menuEl.append(
      item(icon('refresh', { size: 15 }), `Resume in open editor (opened ${n}×)`, () => resumeInEditor(image)),
      item('＋', 'Add as new copy', () => sendToEditor(image, false, 'copy')),
      sep()
    );
  }
  if (image.shared) {
    // A shared (server) row: open / crop the server-stored image. No download/open-in-tab
    // (bytes behind Bearer auth) and no pin (it's already on the server).
    menuEl.append(label('Shared from server'), editSub(image), cropItem(image), ...openInFlat(image));
  } else if (image.kind === 'video') {
    if (image.videoUrl) menuEl.append(
      item(icon('external', { size: 15 }), 'Open video in new tab', () => chrome.tabs.create({ url: image.videoUrl })),
      item(icon('download', { size: 15 }), 'Download video', () => download(image.videoUrl))
    );
    if (editableSrc(image)) {
      if (image.videoUrl) menuEl.append(sep());
      menuEl.append(label('Current frame'), editSub(image), cropItem(image), ...openInFlat(image));
    }
    if (image.posterUrl) {
      const poster = posterImage(image);
      menuEl.append(
        sep(), label('Video preview image'),
        item(icon('external', { size: 15 }), 'Open preview in new tab', () => chrome.tabs.create({ url: poster.src })),
        item(icon('download', { size: 15 }), 'Download preview', () => download(poster.src)),
        editSub(poster), cropItem(poster), ...openInFlat(poster)
      );
    }
    if (pinnable(image)) menuEl.append(sep(), pinSub(image));
  } else {
    menuEl.append(
      item(icon('download', { size: 15 }), 'Download', () => download(image.src)),
      item(icon('external', { size: 15 }), 'Open in new tab', () => chrome.tabs.create({ url: image.src })),
      sep(),
      editSub(image),
      cropItem(image),
      ...openInFlat(image)
    );
    if (pinnable(image)) menuEl.append(pinSub(image));
  }
};

// The image row's ⋯ menu.
const openMenu = (btn, image) => menu.openAnchored(btn, () => buildMenu(image));
// The shared menu with caller-built items — editor mode's per-row menu.
const openMenuNodes = menu.openNodes;
// Open the same menu at a point (used by row right-click); no button is anchored.
const openMenuAt = (image, x, y) => menu.openAt(x, y, () => buildMenu(image));
document.addEventListener('click', (e) => { if (!menuEl.contains(e.target)) closeMenu(); });
listEl.addEventListener('scroll', closeMenu);


// ── Click / double-click gestures on the thumbnail + name ──
// Click → editor, double-click → crop, disambiguated with a short timer. Actions
// are passed in so videos can act on their frame (or open the video in a tab).
const bindGestures = (el, onClick, onDouble) => {
  let timer = null;
  el.addEventListener('click', () => {
    if (timer) return;                      // second click of a dblclick
    timer = setTimeout(() => {
      timer = null;
      if (onClick) run(onClick);
    }, 220);
  });
  el.addEventListener('dblclick', () => {
    if (timer) {
      clearTimeout(timer);
      timer = null;
    }
    if (onDouble) run(onDouble);
  });
};

// A row's single-click action: the ordinary new-tab hand-off, or — in editor mode — an
// import into the editor tab this panel is standing on (openHere, chooser and all).
const openRow = (image, el) => (state.mode === 'editor' ? openHere(image, false, undefined, el) : sendToEditor(image, false));

// Single click → open in the editor (normal, not incognito); double click → crop.
const bindOpenGestures = (el, image) =>
  bindGestures(el, () => openRow(image, el), () => openCrop(image));

const isOpened = (image) => state.markOpened && image.opened && image.opened.length > 0;

const bindRowGestures = (el, image) => {
  // Already-opened image: a click surfaces the resume / add-a-copy chooser (the
  // ⋯ menu) rather than silently creating yet another editor for the same image.
  if (isOpened(image)) {
    bindGestures(el,
      () => { const r = el.getBoundingClientRect(); openMenuAt(image, r.left, r.bottom); },
      () => openCrop(image));
    return;
  }
  if (image.kind !== 'video') return bindOpenGestures(el, image);
  // Video: act on the current frame (or the poster when unplayed); with neither,
  // single-click opens the media in a tab.
  const es = editableSrc(image);
  const onClick = es
    ? () => openRow(image, el)
    : (image.videoUrl ? () => chrome.tabs.create({ url: image.videoUrl }) : null);
  const onDouble = es ? () => openCrop(image) : null;
  bindGestures(el, onClick, onDouble);
};

// Off-screen translucent drag ghost (mirrors browser/js/ui/dragGhost.js): clone the row,
// dim it, and hand it to setDragImage so the cursor carries the row itself, faded — Chrome
// otherwise snapshots the row before any .dragging style applies.
const setTranslucentDragImage = (e, row) => {
  try {
    const ghost = row.cloneNode(true);
    // A bare row has a transparent background, so the OS drag image reads as "just nothing".
    // Give the clone a solid themed card (panel bg + accent outline + shadow) at reduced
    // opacity, so the dragged item shows as a translucent chip.
    ghost.style.cssText += ';position:absolute;top:-9999px;left:-9999px;pointer-events:none;'
      + `width:${row.offsetWidth}px;box-sizing:border-box;opacity:.75;`
      + 'background:var(--panel,#2b2f3a);border:2px solid var(--accent,#7c3aed);'
      + 'border-radius:8px;box-shadow:0 8px 24px rgba(0,0,0,.45);padding:6px;';
    document.body.appendChild(ghost);
    const r = row.getBoundingClientRect();
    e.dataTransfer.setDragImage(ghost, e.clientX - r.left, e.clientY - r.top);
    setTimeout(() => ghost.remove(), 0);
  } catch { /* setDragImage unsupported — the default ghost is fine */ }
};

// Make a row draggable OUT of the panel: onto the page (→ the 4-quadrant drop overlay, side
// panel only in practice) or into any app / the editor tab (→ its global image import). Only
// rows with an openable source drag; the pin / ⋯ buttons keep their own click.
const bindRowDrag = (row, image) => {
  // Prefer the openable bytes URL (a video's captured frame, an image's src); fall back to the
  // provenance source (e.g. a shared row's server URL) so every openable row can drag.
  const src = editableSrc(image) || sourceOf(image);
  if (!src) return;
  row.draggable = true;
  row.addEventListener('dragstart', (e) => {
    if (e.target.closest('button')) { e.preventDefault(); return; }   // pin / ⋯ stay clickable
    try {
      const dt = e.dataTransfer;
      dt.effectAllowed = 'copy';
      dt.setData('text/uri-list', src);
      dt.setData('text/plain', src);
      dt.setData('text/html', `<img src="${src.replace(/"/g, '&quot;')}">`);
      // Our own drag type so the panel's drag-IN handler ignores a row dropped back inside.
      dt.setData('application/x-stencil-drag', image.kind || 'img');
    } catch { /* some contexts lock dataTransfer — the drag still starts */ }
    setTranslucentDragImage(e, row);
    row.classList.add('dragging');
    // Remember WHAT is being dragged: a dragover can read the payload's types but never
    // its data, so the logo's drag menu uses this to know an internal drag's real entry.
    draggingRow = image;
    // Arm the on-page 4-quadrant overlay on the tab in FRONT of the user (in editor mode
    // the editor, not the listed page). Best-effort: only the side panel reliably delivers
    // a drag into the page; popup/DevTools no-op harmlessly.
    const dropTabId = surfaceTabId();
    if (dropTabId != null)
      try { chrome.runtime.sendMessage({ type: MSG.DROPZONES_ARM, tabId: dropTabId }); } catch { /* SW asleep */ }
  });
  row.addEventListener('dragend', () => {
    row.classList.remove('dragging');
    draggingRow = null;
    const dropTabId = surfaceTabId();
    if (dropTabId != null)
      try { chrome.runtime.sendMessage({ type: MSG.DROPZONES_DISARM, tabId: dropTabId }); } catch { /* SW asleep */ }
  });
};

// ── Actions ──
const download = (src) => chrome.downloads.download({ url: src, filename: filenameFromUrl(src) });

// Pin / unpin an image on this site, then re-render so it floats (or settles back).
// The storage write also reaches any open side panel / DevTools panel and the page API
// (entry.pinned) via their storage.onChanged listeners.
const setPinnedState = async (image, pinned) => {
  image.pinned = pinned;
  await setPinned({
    // A merged editor-mode scan spans several pages, so a pin keys on the row's OWN page.
    source: sourceOf(image), site: siteOf(rowResource(image)), resource: rowResource(image),
    name: image.name, kind: image.kind, pinned,
  });
  applyFilters();           // re-sorts + re-renders: a pinned row floats to the top
  // Follow the row to its new position so it stays in view (and flash it), instead of it
  // jumping off-screen while the scroll stays put.
  flashRow(image);
};

// Toggle local pin (used by the row's pin button + the menu's Unpin / "Pin locally").
const togglePin = async (image) => setPinnedState(image, !image.pinned);

// Build a minimal scan-shaped row for a dropped URL that isn't among the scanned images,
// so the new pin still renders (floated + flashed) instead of writing an invisible pin.
// (lib/dropEntry.js owns the shape — the logo drop target normalises to the same rows.)
const rowForDroppedUrl = (src, name) => ({ ...entryFromUrl(src, { name }), pinned: true });

// Drag-to-pin (side panel / DevTools): dropping a page image/video element onto the list pins
// its source. If it matches a scanned row, reuse that row (and its kind/name); otherwise the
// dropped URL is pinned directly and shown as a fresh row. Already pinned → no-op.
const pinFromDroppedUrl = async (url) => {
  const src = String(url || '').trim();
  if (!src) return;
  // A pin needs the page's origin to key on; without it (unscannable page) there's nothing to
  // pin against, so say so rather than writing a pin under an empty site that never shows.
  const site = siteOf(state.activeUrl);
  if (!site) { statusEl.textContent = 'Can’t pin here — this page can’t be scanned.'; return; }
  const pinName = filenameFromUrl(src);
  // Prefer a matching scanned row so the pin carries its real name/kind and floats in place.
  const existing = state.all.find((im) => pinnable(im) && sameSource(sourceOf(im), src));
  if (existing) {
    // Already pinned → no message (that read as a false "not pinned"); just bring its row
    // into view so it's clear it's already there.
    if (existing.pinned) { flashRow(existing); return; }
    // Make sure the pin's outline/float is actually visible even if the toggle was off.
    if (!state.showPinned) await enableShowPinned();
    await setPinnedState(existing, true);
    statusEl.textContent = `Pinned: ${shortName(existing.name)}`;
    return;
  }
  const pins = await loadPins();
  if (isPinnedIn(pins, site, src)) return;   // already pinned (not in this scan) → silent no-op
  // A URL not in this scan (a background element the scanner missed, a cross-frame image):
  // add a row for it so the pin is visible, then write the pin.
  const row = rowForDroppedUrl(src, pinName);
  state.all.push(row);
  await setPinned({ source: src, site, resource: state.activeUrl, name: pinName, kind: guessKindFromUrl(src), pinned: true });
  // Confirm the write actually landed before claiming success (silent storage failures
  // are the difference between "says pinned" and "is pinned").
  const after = await loadPins();
  if (!isPinnedIn(after, site, src)) {
    state.all = state.all.filter((im) => im !== row);
    applyFilters();
    statusEl.textContent = 'Couldn’t save the pin (storage unavailable).';
    return;
  }
  if (!state.showPinned) await enableShowPinned();
  await annotatePinned();
  applyFilters();
  flashRow(row, { landing: true });   // this row exists because of the drop — show it arriving
  statusEl.textContent = `Pinned: ${shortName(pinName)}`;
};

// Turn the "show pinned" view on (persisted) so a just-made pin's gray outline + float are
// visible; drag-to-pin would otherwise write a pin the user can't see.
const enableShowPinned = async () => {
  state.showPinned = true;
  const cb = document.getElementById('f-show-pinned');
  if (cb) cb.checked = true;
  try { await setSettings({ showPinned: true }); } catch { /* setting won't persist */ }
};

// Scroll a freshly-pinned row into view and flash it (shared by drop-pin so a new row is
// noticed even mid-list).
const flashRow = (image, { landing = false } = {}) => {
  const row = state.filtered.includes(image) ? rowElFor(image) : null;
  if (!row) return;
  row.scrollIntoView({ behavior: 'smooth', block: 'center' });
  // A row a DROP just created lands in from the drag and pulses the accent ring;
  // a row that merely got pinned keeps the plainer flash.
  flashLanding(row, landing ? 'just-dropped' : 'just-pinned', 900);
};

// Drag-to-pin on the persistent surfaces (the popup closes on blur mid-drag). Document-level
// listeners so a drop anywhere in the panel counts. Works in the SIDE PANEL; a DevTools panel
// lives in a separate window, so the browser can't hand it a page drag (platform limit).
if (IS_SIDE_PANEL || IS_DEVTOOLS) {
  // Only touch the class on a CHANGE — dragover fires continuously, and re-toggling
  // it restarted the cue's paint (the flicker under the cursor).
  let dragOver = false;
  const setDrag = (on) => {
    if (!!on === dragOver) return;
    dragOver = !!on;
    listEl.classList.toggle('drag-over', dragOver);
  };
  // The Assistant section and the header's brand zone own their drops — don't also pin
  // them here, and don't leave the LIST's cue painted underneath (two drop cues at once
  // read as one solid block).
  const overAssistant = (e) => !!(e.target && e.target.closest
    && e.target.closest('#sec-assistant, header .logo, header h1'));
  // A dragged link/image/text is a drop candidate. URL_DRAG_TYPES, not the assistant's full
  // DRAG_TYPES: a pin keys on a URL, so 'Files' payloads don't qualify here. Both dragenter
  // and dragover must cancel to accept the drop.
  const isDropCandidate = (e) => {
    if (overAssistant(e)) return false;
    const t = e.dataTransfer && e.dataTransfer.types;
    if (!t || t.includes('application/x-stencil-drag')) return false;   // our own row drag → not a pin
    return URL_DRAG_TYPES.some((x) => t.includes(x));
  };
  const accept = (e) => {
    if (!isDropCandidate(e)) {
      if (overAssistant(e)) setDrag(false);
      return;
    }
    e.preventDefault();
    try { e.dataTransfer.dropEffect = 'copy'; } catch { /* noop */ }
    setDrag(true);
  };
  document.addEventListener('dragenter', accept);
  document.addEventListener('dragover', accept);
  document.addEventListener('dragleave', (e) => { if (!e.relatedTarget) setDrag(false); });
  // A cancelled drag (Escape, or a drop outside) still ends — never leave the cue on.
  document.addEventListener('dragend', () => setDrag(false));
  document.addEventListener('drop', (e) => {
    if (!isDropCandidate(e)) return;
    e.preventDefault();
    setDrag(false);
    const url = extractDraggedUrl((type) => { try { return e.dataTransfer.getData(type); } catch { return ''; } });
    if (url) pinFromDroppedUrl(url);
    else statusEl.textContent = 'Couldn’t read a URL from the dropped item (a CSS background image can’t be dragged — use its ⋯ menu).';
  });
}

// Store an already-pinned image on a server as a shared project.
const storeOnServer = async (image, serverUrl) => {
  const conn = serverUrl ? connectionByUrl(state.connections, serverUrl) : null;
  if (!conn) return;
  try {
    statusEl.textContent = `Saving to ${hostLabel(conn.url)}…`;
    await createProject(conn, projectRequestFromImage({ name: image.name, source: sourceOf(image) }, state.activeUrl));
    statusEl.textContent = `Saved to ${hostLabel(conn.url)}.`;
    await loadShared();
    applyFilters();
  } catch (err) {
    statusEl.textContent = `Server save failed: ${err.message}`;
  }
};

// Pin an image, asking WHERE via the target-selector dialog (Cancel aborts entirely).
// `anchor` is the control that asked — the row's pin button or the ⋯ menu's button —
// so the dialog opens next to it rather than covering the panel.
const pinWithPrompt = async (image, anchor) => {
  const target = await promptPinTarget(anchor);   // undefined = cancel, '' = local, url = server
  if (target === undefined) return;            // cancelled — don't pin
  if (!image.pinned) await setPinnedState(image, true);
  if (target) await storeOnServer(image, target);
};

// In-popup dialog asking WHERE to pin (Pin locally / Store on each connected server).
// Resolves the chosen server URL, '' for local, or undefined when cancelled. With an
// `anchor` it opens as a popover next to that control instead of the centred dialog.
const promptPinTarget = (anchor) => openPanelDialog({
  anchor,
  build: (finish) => {
    const title = document.createElement('div');
    title.className = 'dialog-title';
    title.textContent = 'Where do you want to pin this image?';

    const sel = document.createElement('select');
    sel.className = 'dialog-select';
    sel.innerHTML = '<option value="">Pin locally only</option>'
      + state.connections.map((c) => `<option value="${c.url}">Pin & store on ${hostLabel(c.url)}</option>`).join('');
    // Built after the page's own pass, so it asks for its custom list itself — otherwise
    // this one dialog would still open the OS's centred grey popup over the panel.
    queueMicrotask(() => enhanceSelect(sel));

    const row = document.createElement('div');
    row.className = 'dialog-actions';
    const cancel = document.createElement('button');
    cancel.textContent = 'Cancel';
    cancel.addEventListener('click', () => finish(undefined));
    const ok = document.createElement('button');
    ok.className = 'primary';
    ok.textContent = 'Pin';
    ok.addEventListener('click', () => finish(sel.value));
    row.append(cancel, ok);
    return [title, sel, row];
  },
});

// The image bytes to hand to the editor / crop: a shared row pulls them (authed) from its
// server, a page image through the extension's host permissions. An SVG is RASTERISED
// first (lib/rasterize.js) — raw markup has no pixels to hand the editor.
const imageDataUrl = async (image) => {
  if (image.shared) return sharedDataUrl(image);
  const src = editableSrc(image);
  const dataUrl = await fetchAsDataUrl(src, { pageUrl: rowResource(image) });
  if (!isSvgType(mediaTypeOf(dataUrl)) && !isSvgUrl(src)) return dataUrl;
  return rasterizeToPngDataUrl({ dataUrl, width: image.w || 0, height: image.h || 0 });
};

// Hand a URL to the OS / browser from a user gesture. A CUSTOM scheme (stencil://) goes
// through a transient IN-DOCUMENT anchor click — chrome.tabs.create on it leaves a dead
// blank tab (mirrors browser/js/ui/openInModal.js); http(s) opens as a normal new tab.
const openExternalUrl = (url) => {
  if (/^https?:/i.test(url)) { chrome.tabs.create({ url }); return; }
  const a = document.createElement('a');
  a.href = url;
  a.style.display = 'none';
  document.body.appendChild(a);
  a.click();
  a.remove();
};

// "Open in… ▸ Desktop app": a `stencil://open?…` link the OS routes to the desktop app.
// A shared row sends only its server reference (no token in the link); any other row embeds
// its bytes inline, refusing absurdly large payloads (same guards as the browser's modal).
const openInDesktop = async (image) => {
  // Read the scheme SYNCHRONOUSLY from the cached config: a stencil:// launch needs the
  // click's transient user activation, which an `await getSettings()` would spend.
  const desktopScheme = state.openIn && state.openIn.desktopScheme;
  if (!desktopScheme) { statusEl.textContent = 'No desktop app scheme configured (set one in Options).'; return; }
  let url, warn = '';
  if (image.shared && image.serverUrl && image.projectId) {
    url = buildStencilSchemeUrl({ scheme: desktopScheme, server: image.serverUrl, id: image.projectId });
  } else {
    statusEl.textContent = 'Loading image…';
    const dataUrl = await imageDataUrl(image);
    url = buildStencilSchemeUrl({ scheme: desktopScheme, src: dataUrl });
    if (url.length > INLINE_MAX_CHARS) {
      statusEl.textContent = 'Image too large to hand off inline — save it to a server and share the server project instead.';
      return;
    }
    if (url.length > INLINE_WARN_CHARS) warn = ' (large image — if it doesn’t open, save it to a server instead)';
  }
  openExternalUrl(url);
  // Do NOT dismiss() here: window.close() would destroy the document before Chrome acts on
  // the stencil:// anchor navigation — nothing would open. The popup closes on its own when
  // the OS "Open Stencil?" prompt takes focus.
  statusEl.textContent = `Opening in the desktop app…${warn}`;
};

// "Open in… ▸ Telegram bot": a t.me deep link carrying (server, project id) in the 64-char
// ?start= payload — shared rows only (a start payload can't carry bytes); an overflowing
// host points at /connect + /fetch. Only targets the user-connected host of the row.
const openInTelegram = (image) => {
  const telegramBotUsername = state.openIn && state.openIn.telegramBotUsername;
  if (!telegramBotUsername || !image.shared || !image.serverUrl || !image.projectId) return;
  const payload = encodeTelegramStartPayload(image.serverUrl, image.projectId);
  if (!payload) {
    statusEl.textContent = 'The server address is too long for a Telegram link — open the bot and use /connect + /fetch.';
    return;
  }
  openExternalUrl(buildTelegramLink(telegramBotUsername, payload));
  dismiss();
};

const sendToEditor = async (image, incognito, open) => {
  statusEl.textContent = 'Loading image…';
  const { page } = await getSettings();
  const dataUrl = await imageDataUrl(image);
  await openEditorTab(buildHandoff(image, { dataUrl, page, resource: rowResource(image), incognito, open }));
  dismiss();
};

// Resume an already-opened image: jump to the editor tab that's ALREADY open (focus it +
// switch to the matching project, no new tab / no reload). Falls back to the classic new-tab
// resume when no editor tab is open (or its bridge didn't answer).
const resumeInEditor = async (image) => {
  if (await resumeInOpenEditor({ source: sourceOf(image), name: image.name })) {
    dismiss();
    return;
  }
  await sendToEditor(image, false, 'resume');
};

// Same as sendToEditor, but frames the editor in an in-page modal on the active
// page instead of opening a new tab (mirrors the quick-crop modal).
const sendToEditorModal = async (image, incognito, open) => {
  statusEl.textContent = 'Loading image…';
  const { page } = await getSettings();
  const dataUrl = await imageDataUrl(image);
  await launchEditorModal({ ...buildHandoff(image, { dataUrl, page, resource: rowResource(image), incognito, open }), tabId: surfaceTabId() });
  dismiss();
};

// The "open it where I'm looking" action: the in-page modal normally, an import INTO the
// editor tab in editor mode. `anchor` (the asking control) pins the occupied-editor
// chooser next to it; only editor mode uses it.
const openHere = (image, incognito, open, anchor) => (state.mode === 'editor'
  ? editorMode.importHere(image, { incognito, anchor })
  : sendToEditorModal(image, incognito, open));

// Crop opens its in-page modal on the page in FRONT of the user — in editor mode the editor
// tab, not the source page being listed, which the user would never see it on.
const openCrop = async (image) => {
  const src = image.shared ? await sharedDataUrl(image) : editableSrc(image);
  const { source, resource } = buildHandoff(image, { resource: rowResource(image) });
  await launchCrop({ src, source, resource, tabId: surfaceTabId() });
  dismiss();
};

// ── Hover preview ──
// The shared magnifier card (lib/hoverPreview.js): debounce, stale-fetch token, tiny-source
// memo and placement live there; this wires the panel's DOM and fetch path in.
const preview = createHoverPreview({
  previewEl, previewImg, thumbPx: THUMB_PX,
  fetchDataUrl: (src, pageUrl) => fetchAsDataUrl(src, { pageUrl }),
  getSrc: editableSrc,
  getPageUrl: rowResource,
});
// The source → data-URL cache is shared: the row thumbnails' recovery path and the
// shared rows' hand-off reuse bytes a hover already fetched (and vice versa).
const previewCache = preview.cache;
const bindPreview = preview.bind;
const bindDataUrlPreview = preview.bindDataUrl;
const hidePreview = preview.hide;
// A drag suppresses the source element's mouseleave — clear the card on any drag activity.
for (const type of ['dragstart', 'dragend', 'drop']) document.addEventListener(type, hidePreview, true);

// ── Hover-to-highlight the page element ──────────────────────────────────────
// Hovering a row outlines its element on the page (lib/hoverHighlight.js). All row hovers
// share ONE debounced scheduler, so a sweep collapses to the last hovered source and a
// single injected call clears the old outline + sets the new one (no clear/set race).
const HOVER_HL_MS = 100;   // matches the preview debounce — the pointer must settle first
let hoverHlTimer = null;
let hoverHlPending = undefined;   // the last-requested source ('' = clear), or undefined = idle
let hoverHlPendingTab;            // …and the tab it belongs to (a merged scan spans several)
let hoverHlColor = null;          // resolved accent hex, cached across hovers
// `tabId` defaults to the first scanned page; a row from a MERGED editor-mode scan passes
// its own, so hovering it marks the page that image actually lives on.
const runHoverHighlight = async (source, rowTabId) => {
  const tabId = rowTabId != null ? rowTabId : state.activeTabId;
  if (tabId == null) return;
  if (source && hoverHlColor == null) {
    try { hoverHlColor = await highlightColorValue(); } catch { hoverHlColor = '#7c3aed'; }
  }
  await highlightSourceOnTab(tabId, source, hoverHlColor);   // restricted page → false, ignored
};
const scheduleHoverHighlight = (source, rowTabId) => {
  if (!state.hoverHighlight) return;   // feature toggled off (highlight-on-hover checkbox)
  hoverHlPending = source;
  hoverHlPendingTab = rowTabId;
  clearTimeout(hoverHlTimer);
  hoverHlTimer = setTimeout(() => { runHoverHighlight(hoverHlPending, hoverHlPendingTab); }, HOVER_HL_MS);
};
// Bind a row to highlight its page element on hover — every surface, gated by the
// "highlight on hover" checkbox. Shared (server) rows point at a stored project, not a
// live page element, so they're skipped.
const bindHoverHighlight = (rowEl, image) => {
  if (image.shared) return;
  const src = sourceOf(image);
  if (!src) return;
  rowEl.addEventListener('mouseenter', () => scheduleHoverHighlight(src, image.sourceTabId));
  rowEl.addEventListener('mouseleave', () => scheduleHoverHighlight('', image.sourceTabId));
};
// Clear the on-page outline when the surface goes away (side panel / DevTools panel
// persist, so the outline would otherwise linger on the page).
window.addEventListener('pagehide', () => { runHoverHighlight(''); });

// ── Reverse hover: outline the list row for the page element under the cursor ─────
// When the on-page highlight is active it reports the source under the cursor; outline the
// matching row and bring it into view. Only reacts to OUR target tab.
let listHlRow = null;
const highlightListRowForSource = (source) => {
  if (listHlRow) { listHlRow.classList.remove('list-hl'); listHlRow = null; }
  if (!source) return;
  const image = state.filtered.find((im) => !im.shared && pinnable(im) && sameSource(sourceOf(im), source));
  if (!image) return;
  const row = rowElFor(image);
  if (!row) return;
  row.classList.add('list-hl');
  row.scrollIntoView({ block: 'nearest' });
  listHlRow = row;
};
chrome.runtime.onMessage.addListener((msg, sender) => {
  if (msg && msg.type === MSG.HL_HOVER && state.hoverHighlight && sender.tab && sender.tab.id === state.activeTabId) {
    highlightListRowForSource(msg.source || '');
  }
});

// ── Wiring ──
let searchTimer = null;
document.getElementById('f-search').addEventListener('input', () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(applyFilters, 150);
});
['f-regex', 'f-img', 'f-bg', 'f-video', 'f-poster', 'f-meta'].forEach(id => document.getElementById(id).addEventListener('change', applyFilters));

// Opened-images toggles (persisted to settings so they follow the user and stay in
// sync with the options page). "mark opened" needs a re-annotate (badges depend on
// it); "opened first" only re-sorts the current list.
document.getElementById('f-mark-opened').addEventListener('change', async (e) => {
  await setSettings({ markOpened: e.target.checked });
  await annotateOpened();
  applyFilters();
});
document.getElementById('f-opened-first').addEventListener('change', async (e) => {
  await setSettings({ openedFirst: e.target.checked });
  state.openedFirst = e.target.checked;
  applyFilters();
});
// Show-pinned toggle: styles pinned rows (gray outline) and floats them to the top.
// Persisted (follows the user + options page); pinning still works when it's off.
document.getElementById('f-show-pinned').addEventListener('change', async (e) => {
  await setSettings({ showPinned: e.target.checked });
  state.showPinned = e.target.checked;
  applyFilters();
});
// Highlight-on-hover toggle: gates BOTH directions (row→page element, page element→row).
// Persisted (follows the user + the other open surfaces). Turning it off clears any outline
// already showing on the page and any outlined row.
document.getElementById('f-hover-hl').addEventListener('change', async (e) => {
  state.hoverHighlight = e.target.checked;
  await setSettings({ hoverHighlight: e.target.checked });
  if (!e.target.checked) { runHoverHighlight(''); highlightListRowForSource(''); }
});
// Server-pins filter: toggle visibility of server-stored items + per-server narrowing.
document.getElementById('f-server-pins').addEventListener('change', () => { syncServerFilterUI(); applyFilters(); });
document.getElementById('f-server-store').addEventListener('change', applyFilters);

// The highlight lives on the page (survives the popup closing), so on open reflect
// its real state in the checkbox rather than defaulting to unchecked.
const syncHighlightCheckbox = async (tabId) => {
  try {
    const [res] = await chrome.scripting.executeScript({
      target: { tabId }, func: () => !!document.getElementById('stencil-hl-style')
    });
    document.getElementById('f-highlight').checked = !!res?.result;
  } catch {
    /* restricted page — leave as-is */
  }
};

// Highlight toggle: outline every grabbable element on the page. Off by default.
document.getElementById('f-highlight').addEventListener('change', async (e) => {
  if (state.activeTabId == null) { e.target.checked = false; return; }
  try {
    // All frames, so iframed content is highlighted too.
    const color = await highlightColorValue();
    await chrome.scripting.executeScript({
      target: { tabId: state.activeTabId, allFrames: true }, func: toggleStencilHighlight, args: [e.target.checked, color]
    });
  } catch (err) {
    statusEl.textContent = `Couldn’t toggle highlight (${err.message}).`;
    e.target.checked = false;
  }
});
document.getElementById('f-fmt-toggle').addEventListener('click', () => {
  const target = !filterUi.allChecked();
  filterUi.checkboxes().forEach(c => { c.checked = target; });
  filterUi.updateToggleLabel();
  applyFilters();
});
// Collapsible filter sections: the accordion lives in lib/collapsibleSections.js. The
// peek must send a borrowed body home BEFORE the class flips; a user toggle cancels a
// drag's queued fold-back (both wired lazily — their owners are created just below).
const sections = createCollapsibleSections({
  doc: document,
  beforeToggle: (section) => sectionPeek.sectionToggled(section),
  onUserToggle: (id) => dragSections.manualToggle(id),
});
// The Search section owns the results: collapsing it folds the list + status too.
sections.setHook(SEARCH_SECTION, (collapsed) => document.body.classList.toggle('search-collapsed', collapsed));

// ── Alt + hover peek: a collapsed section's body in a floating mini window ───
// Hold Alt and hover a folded header (either order) to see the content WITHOUT unfolding
// the accordion. The panel borrows the REAL .section-body node (wiring intact) and returns
// it on close. Rules and timers live in lib/sectionPeek.js; this is only the DOM.
const peekPanel = document.createElement('div');
peekPanel.id = 'section-peek';
peekPanel.hidden = true;
const peekTitle = document.createElement('div');
peekTitle.className = 'peek-title';
peekPanel.appendChild(peekTitle);
document.body.appendChild(peekPanel);
let peekHome = null;   // { body, parent, next } — where the borrowed body goes back
const sectionPeek = createSectionPeek({
  isCollapsed: (s) => !!s && !s.hidden && s.classList.contains('collapsed'),
  // Engaged = pointer inside, or a text field with typed content (a focused
  // checkbox or empty field must not pin the panel).
  isEngaged: () => {
    if (peekPanel.hidden) return false;
    if (peekPanel.matches(':hover')) return true;
    const a = document.activeElement;
    return !!a && peekPanel.contains(a) && isTypingTarget(a) && String(a.value ?? '').trim() !== '';
  },
  open: (s) => {
    const head = s.querySelector('.section-head');
    const body = s.querySelector('.section-body');
    if (!head || !body) return;
    // The assistant boots lazily on its first expand — a peek counts as one.
    if (s.id === ASSISTANT_SECTION) sections.runHook(s.id, false);
    peekHome = { body, parent: body.parentNode, next: body.nextSibling };
    peekTitle.textContent = head.querySelector('.dlbl')?.textContent || '';
    peekPanel.appendChild(body);
    peekPanel.hidden = false;
    // Measure AFTER it shows — the placement needs the panel's real size.
    const box = peekPanel.getBoundingClientRect();
    const p = peekPosition({
      anchor: head.getBoundingClientRect(),
      box: { width: box.width, height: box.height },
      viewport: { width: window.innerWidth, height: window.innerHeight },
    });
    peekPanel.style.left = `${p.left}px`;
    peekPanel.style.top = `${p.top}px`;
  },
  close: () => {
    if (peekHome) peekHome.parent.insertBefore(peekHome.body, peekHome.next);
    peekHome = null;
    peekPanel.hidden = true;
  },
});
document.addEventListener('mouseover', (e) => {
  const head = e.target.closest?.('.section-head');
  // The mouse route always peeks — gliding between headers is deliberate; only
  // the Alt KEY-press route defers to a focused text control (typing).
  if (head) { sectionPeek.enterHead(head.closest('.fsection'), e.altKey); return; }
  if (e.target.closest?.('#section-peek')) sectionPeek.enterPeek();
});
document.addEventListener('mouseout', (e) => {
  const from = e.target.closest?.('.section-head, #section-peek');
  if (!from) return;
  const to = e.relatedTarget;
  if (to && to.closest && to.closest('.section-head, #section-peek') === from) return;   // still inside
  sectionPeek.leave();
});
document.addEventListener('keydown', (e) => {
  if (e.key === 'Alt' && !e.repeat) {
    // While a text control has focus, Alt belongs to the typing (Alt+letter
    // characters, input shortcuts) — never steal it for the peek.
    if (isTypingTarget(document.activeElement)) return;
    const head = document.querySelector('.section-head:hover');
    // preventDefault keeps the bare Alt from focusing the browser's menu bar
    // while it is being used as the peek key.
    if (head) { e.preventDefault(); sectionPeek.altPressed(head.closest('.fsection')); }
  } else if (e.key === 'Escape' && sectionPeek.isOpen()) {
    e.preventDefault();
    sectionPeek.dismiss();
  }
});
// HOLD-to-peek: the panel lives only while Alt is down. Blur too — Alt+Tab
// switches away without ever delivering the keyup.
document.addEventListener('keyup', (e) => { if (e.key === 'Alt') sectionPeek.altReleased(); });
window.addEventListener('blur', () => sectionPeek.altReleased());
// A press anywhere outside the panel closes the peek (a press on a header then
// toggles that section normally — sectionToggled has already sent the body home).
document.addEventListener('pointerdown', (e) => {
  if (sectionPeek.isOpen() && !e.target.closest?.('#section-peek')) sectionPeek.dismiss();
}, true);

// ── Spring-loaded drop targets: a COLLAPSED section can't accept a drop ──────
// While a drag is live, the section the POINTER dwells on unfolds — only that one — and
// folds back if the drag ends elsewhere (lib/dragSections.js owns the rules). A HIDDEN
// section (assistant with provider off) is absent, never sprung.
const dragSections = createDragSectionOpener({
  sections: (IS_SIDE_PANEL || IS_DEVTOOLS) ? [ASSISTANT_SECTION, SEARCH_SECTION] : [ASSISTANT_SECTION],
  isCollapsed: sections.isCollapsed,
  expand: (id) => sections.setCollapsed(id, false),
  collapse: (id) => sections.setCollapsed(id, true),
  // Only scroll if the freshly unfolded body isn't fully visible — the pointer is
  // already ON this section, so the layout must move as little as possible under it.
  onOpen: (id) => document.getElementById(id)?.scrollIntoView({ block: 'nearest' }),
});

// What is being dragged: our own list rows carry the x-stencil-drag type; anything
// else must look like an image/video payload (the same kinds chatDrop.js classifies).
const dragKind = (e) => dragPayloadKind(e.dataTransfer && e.dataTransfer.types);
// The collapsible section under the pointer — for a collapsed one that's its header
// row, which is all of it that's left on screen.
const sectionUnder = (node) => (node && node.closest ? (node.closest('.fsection')?.id || '') : '');
for (const type of ['dragenter', 'dragover']) {
  document.addEventListener(type, (e) => {
    // Any compatible drag anywhere over the surface advertises the logo as a target
    // (it pulses) — the point is to say "you can drop here" BEFORE the pointer arrives.
    logoMenu.armUpdate(e.dataTransfer && e.dataTransfer.types);
    const kind = dragKind(e);
    if (kind) dragSections.pointerOver(kind, sectionUnder(e.target));
  }, true);
}
// A drop INSIDE an auto-opened section keeps it open (capture phase, so it is recorded
// before the section's own drop handler and before the document-level `end()` below).
document.getElementById(ASSISTANT_SECTION)?.addEventListener('drop', () => dragSections.dropIn(ASSISTANT_SECTION), true);
listEl.addEventListener('drop', () => dragSections.dropIn(SEARCH_SECTION), true);
// ── Header logo: a SPRING-LOADED drag menu for media dragged off the PAGE ────
// The whole mechanism — spring dwell, grace timer, drop-only items, the once-per-release
// gate — lives in lib/logoDragMenu.js; this wires its document-level end-of-drag paths.
// The row being dragged out of our own list, so an internal drag knows its exact entry
// (a `dragover` exposes the DataTransfer's TYPES but never its data).
let draggingRow = null;

// Perform one drag-menu action on the released payload. The entry is normalised at
// DROP time (the only moment the payload is readable), so the optimistic menu is
// re-checked here: an action that turns out not to apply says so instead of throwing.
const runDragMenuAction = (id, payload) => {
  const entry = entryFromDrop(payload, { items: state.all, objectUrl: (f) => URL.createObjectURL(f) });
  if (!entry) { statusEl.textContent = 'Couldn’t read an image or video from that drop.'; return; }
  if (!dragActionAllowed(entry, id)) {
    statusEl.textContent = `“${shortName(entry.name)}” has no image to ${id === 'crop' ? 'crop' : 'open in the editor'} — try “Open in new tab”.`;
    return;
  }
  if (id === 'newtab') { chrome.tabs.create({ url: sourceOf(entry) }); return; }
  if (id === 'crop') { run(() => openCrop(entry)); return; }
  // "Open in editor" is the row menu's own default: the in-page modal ("Here") — or, in
  // editor mode, an import into the editor tab the panel is standing on.
  run(() => openHere(entry, id === 'incognito'));
};

const logoMenu = createLogoDragMenu({
  logoEl: document.querySelector('header .logo'),
  menuEl,
  placeMenu,
  closeSharedMenu: closeMenu,
  dragKind,
  getDraggingRow: () => draggingRow,
  onAction: runDragMenuAction,
  springMs: SPRING_DWELL_MS,   // dwell before the menu springs open (matches the section spring)
});

// ONE document dragleave listener runs the three end-of-drag branches in the order they
// used to be registered; each keeps its own guard and they touch disjoint state.
document.addEventListener('dragleave', (e) => {
  logoMenu.graceOnDragLeave(e);
  if (!e.relatedTarget) {
    // The drag left the window: stop advertising at once (the pointer is gone), but
    // only SCHEDULE the section fold-back in case it comes back.
    logoMenu.armEnd();
    dragSections.scheduleEnd();
  } else {
    // Leaving a section (into a sibling) must also cancel a dwell that hasn't sprung
    // yet — pointerOver('') does that; the drag is still live.
    const kind = dragKind(e);
    if (kind && sectionUnder(e.relatedTarget) !== sectionUnder(e.target)) {
      dragSections.pointerOver(kind, sectionUnder(e.relatedTarget));
    }
  }
});
// Released anywhere but on an item (the item's own handler stops that drop), the drag
// ending, or Escape: close and do nothing; a drop also folds the drag-out sections back.
document.addEventListener('drop', () => { logoMenu.release(); dragSections.end(); });
document.addEventListener('keydown', (e) => { if (e.key === 'Escape') logoMenu.dismiss(); });
document.addEventListener('dragend', () => { logoMenu.release(); dragSections.end(); });
['f-minw', 'f-maxw', 'f-minh', 'f-maxh'].forEach(id => document.getElementById(id).addEventListener('input', () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(applyFilters, 150);
}));
document.getElementById('rescan').addEventListener('click', scan);
// Cached while the context is alive: after an extension reload an already-open
// DevTools panel is INVALIDATED — every chrome.* touch throws — but a plain
// window.open on this pre-computed URL still lands in the fresh extension.
const optionsUrl = (() => {
  try { return chrome.runtime.getURL('src/options/options.html'); } catch { return null; }
})();
document.getElementById('open-options').addEventListener('click', () => {
  // The DevTools panel reuses this script, and its context has no
  // chrome.runtime.openOptionsPage — route through the service worker there.
  try {
    if (typeof chrome.runtime.openOptionsPage === 'function') { chrome.runtime.openOptionsPage(); return; }
    chrome.runtime.sendMessage({ type: MSG.OPEN_OPTIONS });
  } catch {
    // Invalidated context (the extension reloaded under this panel): best
    // effort — open the options page as an ordinary tab, never throw.
    if (optionsUrl) window.open(optionsUrl, '_blank');
  }
});

// Dark / light toggle (mirrors the editor's moon button). It pins the opposite of
// what's PAINTED, so the first click flips what you see even while the mode is still
// 'system'; Options offers the full System / Light / Dark choice.
const themePref = window.StencilTheme;
const themeBtn = document.getElementById('theme-toggle');
if (themePref && themeBtn) {
  const syncThemeBtn = () => {
    const dark = themePref.resolved() === 'dark';
    themeBtn.innerHTML = icon(dark ? 'sun' : 'moon');
    themeBtn.title = dark ? 'Switch to the light theme' : 'Switch to the dark theme';
  };
  themeBtn.addEventListener('click', () => {
    // Pass the button itself, so the palette floods out of it and never has to guess.
    themePref.set(themePref.resolved() === 'dark' ? 'light' : 'dark', themeBtn);
    syncThemeBtn();
  });
  themePref.onChange(syncThemeBtn);
  syncThemeBtn();
}

// Editor mode: the two extra sections shown when this panel stands ON the Stencil editor —
// the open-editor list, the source-page picker and the import-into-this-editor path. The
// menu is handed over whole, so an editor row's ⋯ is the row menu.
const editorMode = createEditorMode({
  setStatus: (text) => { statusEl.textContent = text; },
  run,
  dismiss,
  menu: { item, submenu, open: openMenuNodes, close: closeMenu },
  // Pages were ticked (or re-scanned): the ordinary list below re-scans into them. If the
  // results section is folded, unfold it — ticking a page and seeing nothing appear reads as
  // "the picker is broken" when the images are really just hidden behind a collapsed header.
  onSourceTab: (picked) => {
    if (picked && picked.length) setSectionCollapsed(SEARCH_SECTION, false);
    scan();
  },
  // The bytes an import hands over, resolved by the SAME panel-side path every other open
  // action uses — so an SVG row imports as the rasterised PNG here too, not as raw markup
  // the service worker has no DOM to draw.
  imageDataUrl,
  // The panel's floating magnifier, so an editor row's canvas preview enlarges on hover
  // exactly as an image row's thumbnail does.
  preview: { bind: bindDataUrlPreview, hide: hidePreview },
});

// AI assistant (llm-contract.md §8): the embedded, collapsed-by-default section, chatting
// over the LIVE scan state; boots lazily on first expansion, state lives with this document.
// ── The assistant driving the panel's OWN controls (contract §8 theme / filter) ──
// Both go through the same DOM controls a click would use and then the normal applyFilters
// pass, so the controls, persisted state and list can never disagree with the model.
const KIND_CONTROL = {
  images: 'f-img', css: 'f-bg', video: 'f-video', posters: 'f-poster', meta: 'f-meta',
};
const assistantSetTheme = (mode) => {
  if (!themePref) throw new Error('the theme cannot be changed here');
  themePref.set(mode, document.getElementById('theme-toggle'));
};
const assistantSetFilters = (patch) => {
  const applied = [];
  const setValue = (id, v) => { const el = document.getElementById(id); if (el) el.value = v; };
  if (patch.search != null) { setValue('f-search', patch.search); applied.push(patch.search ? `search "${patch.search}"` : 'search cleared'); }
  if (patch.regex != null) {
    const el = document.getElementById('f-regex');
    if (el) { el.checked = patch.regex; applied.push(`regex ${patch.regex ? 'on' : 'off'}`); }
  }
  if (patch.kinds) {
    // The listed kinds go ON and every other kind OFF — the model is stating the whole
    // set it wants shown, not toggling one box.
    const want = new Set(patch.kinds);
    for (const [kind, id] of Object.entries(KIND_CONTROL)) {
      const el = document.getElementById(id);
      if (el) el.checked = want.has(kind);
    }
    applied.push(`showing ${patch.kinds.join(', ') || 'nothing'}`);
  }
  if (patch.formats) {
    const all = patch.formats.includes('*');
    const want = new Set(patch.formats);
    for (const cb of filterUi.checkboxes()) cb.checked = all || want.has(String(cb.value).toUpperCase());
    filterUi.updateToggleLabel();
    applied.push(all ? 'all formats' : `formats ${patch.formats.join(', ') || 'cleared'}`);
  }
  // A bound of 0 CLEARS it (an empty input is "no bound"), which is what "any width"
  // has to mean — 0 as a literal minimum would filter nothing anyway.
  for (const [key, id, label] of [['minWidth', 'f-minw', 'min width'], ['maxWidth', 'f-maxw', 'max width'],
    ['minHeight', 'f-minh', 'min height'], ['maxHeight', 'f-maxh', 'max height']]) {
    if (patch[key] == null) continue;
    setValue(id, patch[key] ? String(patch[key]) : '');
    applied.push(patch[key] ? `${label} ${patch[key]}px` : `${label} cleared`);
  }
  // The three list toggles (§8 panel-op widening): set the checkbox and fire its own
  // change handler, so the persisted setting, the state flags and the re-annotate all
  // run the exact path a click takes.
  for (const [key, id, label] of [['markOpened', 'f-mark-opened', 'mark opened'],
    ['openedFirst', 'f-opened-first', 'opened first'], ['showPinned', 'f-show-pinned', 'show pinned']]) {
    if (patch[key] == null) continue;
    const el = document.getElementById(id);
    if (el) { el.checked = patch[key]; el.dispatchEvent(new Event('change')); }
    applied.push(`${label} ${patch[key] ? 'on' : 'off'}`);
  }
  applyFilters();   // …and the list, the count and the persisted state follow
  return { applied };
};

// The §8 accent op → the Options accent path (lib/accent.js). The store holds preset KEYS
// only, so a raw "#rrggbb" resolves to the closest preset. StencilAccent.set persists,
// mirrors to other open surfaces, and runs the same palette-swap the dropdown does.
const assistantSetAccent = ({ color, preset }) => {
  const accentPref = window.StencilAccent;
  if (!accentPref) throw new Error('the accent cannot be changed here');
  let hit = null, exact = true;
  if (preset != null) {
    const want = preset.toLowerCase();
    hit = accentPref.list.find((a) => a.key === want || a.label.toLowerCase() === want);
    if (!hit) throw new Error(`unknown accent preset "${preset}"`);
  } else {
    const rgb = (hex) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
    const want = rgb(color);
    let bestD = Infinity;
    for (const a of accentPref.list) {
      const c = rgb(a.hex);
      const d = (c[0] - want[0]) ** 2 + (c[1] - want[1]) ** 2 + (c[2] - want[2]) ** 2;
      if (d < bestD) { bestD = d; hit = a; }
    }
    exact = bestD === 0;
  }
  accentPref.set(hit.key, document.getElementById('theme-toggle'));
  return { label: hit.label, exact };
};

const assistant = createAssistant({
  getItems: () => state.all,
  getTabId: () => state.activeTabId,
  getPageUrl: () => state.activeUrl,
  openHere: (entry, opts) => (state.mode === 'editor' ? editorMode.importHere(entry, opts) : false),
  // The model's `pin` op rides the popup's own pin path (persist + re-sort); an
  // entry from a scanTab'd working set pins on its own site via its `resource`.
  // Same gate as the row's pin button: no stable source URL → not pinnable.
  pinImage: (entry) => {
    if (!pinnable(entry)) throw new Error('this image has no stable source URL to pin');
    return setPinnedState(entry, true);
  },
  // §8 unpin — the same path in reverse (local pins only, like the row's Unpin).
  unpinImage: (entry) => {
    if (!pinnable(entry)) throw new Error('this image has no stable source URL to pin');
    return setPinnedState(entry, false);
  },
  // §8 rescan — the popup's own scan refreshes state.all, which getItems rides.
  rescan: () => scan(),
  setTheme: assistantSetTheme,
  setFilters: assistantSetFilters,
  setAccent: assistantSetAccent,
});
// The Assistant section boots its chat UI lazily on first expansion.
sections.setHook(ASSISTANT_SECTION, (collapsed) => assistant.handleToggle(collapsed));
const chatBtn = document.getElementById('open-chat');
chatBtn.innerHTML = icon('sparkle');
chatBtn.addEventListener('click', () => assistant.reveal());

// Assistant OFF (provider 'none', contract §5) → the section and its ✦ button don't exist
// for the user at all, and the controller never boots. The section stays in the DOM but
// `hidden`, so Options re-enables it live — and hidden ≠ collapsed, so the drag spring skips it.
const applyAssistantGate = async () => {
  applyAssistantVisibility(assistantEnabled(await loadLlmSettings()), {
    section: document.getElementById(ASSISTANT_SECTION),
    button: chatBtn,
  });
};
applyAssistantGate();
chrome.storage.onChanged.addListener((changes, area) => {
  // Picking a provider in Options re-shows it live (and choosing "off" hides it).
  if (area === 'local' && changes[LLM_SETTINGS_KEY]) applyAssistantGate();
});

// Popup only: promote this view into the docked side panel (same UI, but it persists
// while you work the page and re-scans on tab switch). Opening a side panel needs a
// user gesture, which this click is; closing the popup hands focus to the panel.
document.getElementById('open-sidepanel')?.addEventListener('click', async () => {
  try {
    const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
    await chrome.sidePanel.open({ windowId: tab.windowId });
    window.close();
  } catch (err) {
    statusEl.textContent = `Couldn’t open the side panel (${err.message}).`;
  }
});

// Side panel only: it outlives a single page, so re-scan when the user switches tabs
// or the active tab finishes loading new content (the popup, which closes on blur,
// just scans once on open). Guard re-entrancy isn't needed — scan() resets state.
if (IS_SIDE_PANEL) {
  chrome.tabs.onActivated.addListener(() => scan());
  chrome.tabs.onUpdated.addListener((_id, info, tab) => {
    if (tab.active && info.status === 'complete') scan();
  });
} else if (IS_DEVTOOLS) {
  // A DevTools panel is pinned to one tab and never switches; it only needs to
  // re-scan when that inspected page navigates to fresh content.
  chrome.devtools.network.onNavigated.addListener(() => scan());
}

// The ledger can change while a surface is open — notably a prune when a project is
// deleted in the editor (background.js → pruneLedger). Re-annotate scanned images in
// place so badges drop without a re-scan. Mainly serves the side panel / DevTools panel.
chrome.storage.onChanged.addListener((changes, area) => {
  if (area === 'local' && changes[LEDGER_KEY] && state.all.length) {
    annotateOpened().then(applyFilters);
  }
  // Pins changed elsewhere (this surface, another open surface, or the page API) —
  // re-annotate in place so the gray outline / float updates without a re-scan.
  if (area === 'local' && changes[PINS_KEY] && state.all.length) {
    annotatePinned().then(applyFilters);
  }
  // Connections added/removed (Options page, or another surface) — re-pull shared pins
  // and (re)start polling so the golden-outlined rows appear/disappear without a rescan.
  if (area === 'local' && changes[CONNECTIONS_KEY]) {
    loadShared().then(() => {
      startSharedPolling();
      applyFilters();
    });
  }
  // Keep concurrently-open surfaces in lockstep: the popup, side panel, and DevTools
  // panel all run this controller and persist their filter state to the same key, so a
  // change in one should mirror into the others. Skip the echo of our own write.
  if (area === 'local' && changes[FILTERS_KEY]) {
    // Skips the echo of our own write; otherwise re-syncs the controls + the list.
    if (filterUi.acceptExternal(changes[FILTERS_KEY].newValue || null)) applyFilters();
  }
  // The opened-images settings (markOpened / openedFirst) live in storage.sync and are
  // also editable from the options page — reflect external changes here too.
  if (area === 'sync' && (changes.markOpened || changes.openedFirst) && state.all.length) {
    annotateOpened().then(applyFilters);   // annotateOpened re-syncs the two checkboxes
  }
  // The show-pinned setting also lives in storage.sync and is editable from options.
  if (area === 'sync' && changes.showPinned && state.all.length) {
    annotatePinned().then(applyFilters);   // annotatePinned re-syncs its checkbox
  }
  // The "Open in…" targets (desktop scheme / Telegram bot username) are edited on the
  // options page — refresh the cache so the ⋯ menu's Open-in items gate correctly without
  // a rescan (menus are built lazily on click, so no re-render is needed).
  if (area === 'sync' && (changes.desktopScheme || changes.telegramBotUsername)) {
    loadOpenInSettings();
  }
  // The highlight-on-hover setting changed in another open surface — mirror it here
  // (no re-annotate needed: it only gates the hover behaviour, not the list contents).
  if (area === 'sync' && changes.hoverHighlight) {
    state.hoverHighlight = changes.hoverHighlight.newValue !== false;
    const hh = document.getElementById('f-hover-hl');
    if (hh) hh.checked = state.hoverHighlight;
    if (!state.hoverHighlight) { runHoverHighlight(''); highlightListRowForSource(''); }
  }
});

// Load the persisted filters first, restore the static controls, then scan (the pill
// rebuild restores the format toggles from the same persisted state).
// Numeric fields (the size filters) take an expression — "45 + 9", "* 2".
watchNumericInputs();

filterUi.load().then(() => { filterUi.restoreStatic(); scan(); });

// Instant, structured tooltips everywhere on this page (the native `title` waits ~1s
// and never shows on a disabled control). lib/tipContent.js gives them their shape.
initTooltips();

// Same for the popup's own filter selects — in a 400px window the OS list covers the page.
for (const el of document.querySelectorAll('select')) enhanceSelect(el);
