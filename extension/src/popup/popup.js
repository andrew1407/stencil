// ── Popup: list, filter, and act on every image on the active page ───────────
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchEditorModal, launchCrop, getSettings, setSettings, blobToDataUrl } from '../lib/stencil.js';
import { LEDGER_KEY, loadLedger, matchEntries, trackableSource } from '../lib/ledger.js';
import { PINS_KEY, loadPins, isPinnedIn, siteOf, setPinned, projectNameColor } from '../lib/pins.js';
import { resolveHighlightColor } from '../lib/highlightColor.js';
import { scanPageForImages } from '../lib/imageScan.js';
import { toggleStencilHighlight } from '../lib/highlight.js';
import { icon } from '../lib/icons.js';
import { passesFilters, distinctFormats, formatOf, formatOfItem, UNKNOWN_FORMAT, VIDEO_FORMATS } from '../lib/filters.js';
import {
  CONNECTIONS_KEY, loadConnections, collectSharedPins, connectionByUrl,
  createProject, fetchProjectImage, pinTargetMode, projectRequestFromImage,
} from '../lib/connections.js';

// Common web image formats always offered in the filter, plus any others the page
// uses (added in populateFormats) and the video container formats (VIDEO_FORMATS).
const COMMON_FORMATS = ['png', 'jpg', 'gif', 'webp', 'svg', 'avif', 'bmp', 'ico', 'tiff'];

const listEl = document.getElementById('list');
const statusEl = document.getElementById('status');
const countEl = document.getElementById('count');
const previewEl = document.getElementById('preview');
const previewImg = previewEl.querySelector('img');
const menuEl = document.getElementById('action-menu');

const MAX_IMAGES = 1000; // hard cap on what we pull from the page
const THUMB_PX = 48;     // rendered thumbnail size (see .thumb in popup.css)
// Page schemes the extension can't script.
const BLOCKED_SCHEMES = ['chrome:', 'edge:', 'about:', 'chrome-extension:', 'view-source:'];
// Placeholder thumbnail for a video whose frame couldn't be read (cross-origin).
const PLAY_THUMB = 'data:image/svg+xml,' + encodeURIComponent(
  '<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48"><rect width="48" height="48" fill="#2b2f3a"/><polygon points="19,15 35,24 19,33" fill="#7c3aed"/></svg>');

const state = { all: [], filtered: [], activeTabId: null, activeUrl: '', markOpened: true, openedFirst: true, showPinned: true, connections: [], shared: [] };

// How often (ms) the popup re-pulls shared pins from connected servers while it's
// open. MV3 popups are short-lived, so a light poll-while-open is both simple and
// correct — no background WS to keep alive. Cleared when the surface unloads.
const SHARED_POLL_MS = 8000;

// This controller drives three surfaces: the toolbar popup (closes after an action),
// the docked side panel (src/sidepanel/sidepanel.html), and the DevTools panel
// (src/devtools/panel.html). Docked ones stay open and re-scan; only the popup closes.
// The host document's path tells them apart.
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

// The image's own URL for provenance: its media URL for a video (the still is an
// opaque frame), else the image/background src. Empty/data: sources aren't tracked.
const sourceOf = (image) => (image.kind === 'video' ? (image.videoUrl || '') : (image.src || ''));

// A video's poster as a standalone image item, so the action menu can open / crop /
// download the poster (preview cover) directly — the same actions the page context
// menu's "Video preview image" submenu offers. Mirrors a scanned <img> poster row.
const posterImage = (video) => ({
  kind: 'img',
  src: video.posterUrl,
  poster: true,
  name: filenameFromUrl(video.posterUrl, 'poster'),
  w: 0, h: 0
});

// The image to actually open / crop / preview: the scanned still, falling back to a
// video's poster when no frame was captured (an unplayed video shows its poster, and
// its frame 0 is often black — so the poster is the right stand-in, never a black image).
const editableSrc = (image) => image.src || image.posterUrl || '';

// Measure unknown-size images only once their row scrolls into view (thumbnails
// use loading="lazy" too). All matching rows render up front so filtering shows all.
const measureObs = new IntersectionObserver((entries) => {
  for (const e of entries) {
    if (!e.isIntersecting) continue;
    measureObs.unobserve(e.target);
    measure(e.target._image, e.target._dimEl, e.target);
  }
}, { root: listEl, rootMargin: '200px' });

// ── Scan ──
const scan = async () => {
  listEl.innerHTML = '';
  statusEl.textContent = 'Scanning…';
  // Render the format checkboxes up front (common formats), so they're always
  // visible even on a page that can't be scanned; refreshed once results arrive.
  state.all = [];
  populateFormats();
  const tab = await getTargetTab();
  if (!tab || BLOCKED_SCHEMES.some(s => (tab.url || '').startsWith(s))) {
    statusEl.textContent = 'This page can’t be scanned.';
    return;
  }
  state.activeTabId = tab.id;
  state.activeUrl = tab.url || '';
  await syncHighlightCheckbox(tab.id);
  let images = [];
  try {
    // Scan every frame (content is often in an iframe), then dedupe across frames.
    const results = await chrome.scripting.executeScript({
      target: { tabId: tab.id, allFrames: true }, func: scanPageForImages, args: [MAX_IMAGES]
    });
    const seen = new Set();
    for (const r of results) {
      for (const it of (r?.result || [])) {
        if (images.length >= MAX_IMAGES) break;
        if (!seen.has(it.src)) { seen.add(it.src); images.push(it); }
      }
    }
  } catch (err) {
    statusEl.textContent = `Could not read this page (${err.message}).`;
    return;
  }
  state.all = images.map(it => ({
    ...it,
    // Name a video from its media URL (the still is an opaque data URL); fall back
    // to a generic name when the video is an in-page blob with no usable URL.
    name: filenameFromUrl(it.kind === 'video' && it.videoUrl ? it.videoUrl : it.src, it.kind === 'video' ? 'video' : 'image'),
    measured: it.w > 0 && it.h > 0
  }));
  await annotateOpened();
  await annotatePinned();
  await loadShared();
  startSharedPolling();
  populateFormats();
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

// An image/video can be pinned when it has an openable source URL (img/background src,
// or a video's media URL) — the same thing "open in new tab" needs.
const pinnable = (image) => !!sourceOf(image);

// Tag each image with whether it's pinned on this site (drives the gray outline, the
// pin button's active state, and the float-to-top sort). The pin store is keyed by the
// page's origin so a pin made here matches the same image on the same site next visit.
const annotatePinned = async () => {
  const { showPinned } = await getSettings();
  state.showPinned = showPinned;
  document.getElementById('f-show-pinned').checked = showPinned;
  const site = siteOf(state.activeUrl);
  const pins = await loadPins();
  for (const img of state.all) img.pinned = pinnable(img) && isPinnedIn(pins, site, sourceOf(img));
};

const isPinned = (image) => state.showPinned && !!image.pinned;

// ── Shared pins (connected collaboration servers) ───────────────────────────
// A server project (with an image) becomes a SHARED pin row: it renders alongside
// the page's own images with a golden outline + server badge, and its thumbnail /
// editor hand-off are fetched over the server's Bearer-authed download endpoint.
const SHARED_POLL = { timer: null };

// Map a shared-pin record (from connections.js) to a row in the popup's image shape.
// No `src` (the download is authed — a bare <img> can't load it); the thumbnail and
// open paths resolve the bytes via fetchProjectImage instead. measured:true keeps the
// size observer off a row that has no probe-able URL.
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

// Build a checkbox per format (common ones + any extra the page uses). All start
// checked (= no filtering); the toggle button flips select-all / deselect-all.
const populateFormats = () => {
  const present = new Set(distinctFormats(state.all));
  // 'etc' (undetectable format) is always offered, last, marked present only when
  // the page actually has such items.
  if (state.all.some(it => !formatOfItem(it))) present.add(UNKNOWN_FORMAT);
  const known = new Set([...COMMON_FORMATS, ...VIDEO_FORMATS, UNKNOWN_FORMAT]);
  const extras = [...present].filter(f => !known.has(f));
  const formats = [...COMMON_FORMATS, ...VIDEO_FORMATS, ...extras, UNKNOWN_FORMAT];
  const box = document.getElementById('f-formats');
  box.innerHTML = formats.map(f =>
    `<label class="${present.has(f) ? '' : 'absent'}"><input type="checkbox" value="${f}" checked>${f.toUpperCase()}</label>`
  ).join('');
  box.querySelectorAll('input').forEach(cb =>
    cb.addEventListener('change', () => {
      updateToggleLabel();
      applyFilters();
    }));
  // Re-apply persisted format toggles (checkboxes are rebuilt fresh — all checked — on
  // every scan, so restore the user's OFF formats here). New formats default to on.
  applyPersistedFormats();
};

// Sync the (already-rendered) format checkboxes to persistedFilters.disabledFormats:
// OFF formats unchecked, everything else (incl. formats new since the save) checked.
// Used after a scan rebuilds them and when another open surface changes the filters.
const applyPersistedFormats = () => {
  const box = document.getElementById('f-formats');
  if (!box) return;
  const off = new Set(persistedFilters && Array.isArray(persistedFilters.disabledFormats)
    ? persistedFilters.disabledFormats : []);
  box.querySelectorAll('input').forEach(cb => { cb.checked = !off.has(cb.value); });
  updateToggleLabel();
};

const formatCheckboxes = () => [...document.getElementById('f-formats').querySelectorAll('input')];
const allFormatsChecked = () => {
  const cbs = formatCheckboxes();
  return cbs.length > 0 && cbs.every(c => c.checked);
};
const updateToggleLabel = () => { document.getElementById('f-fmt-toggle').textContent = allFormatsChecked() ? 'Deselect all' : 'Select all'; };

// ── Filtering ──
const readFilters = () => {
  const num = el => {
    const v = parseFloat(el.value);
    return isNaN(v) ? null : v;
  };
  return {
    search: document.getElementById('f-search').value.trim(),
    formats: formatCheckboxes().filter(c => c.checked).map(c => c.value),
    minW: num(document.getElementById('f-minw')),
    maxW: num(document.getElementById('f-maxw')),
    minH: num(document.getElementById('f-minh')),
    maxH: num(document.getElementById('f-maxh')),
    includeImg: document.getElementById('f-img').checked,
    includeBg: document.getElementById('f-bg').checked,
    includeVideo: document.getElementById('f-video').checked,
    includePosters: document.getElementById('f-poster').checked
  };
};

let filters = {};
const renderCount = () => {
  const total = state.all.length + state.shared.length;
  countEl.textContent = total ? `(${state.filtered.length}/${total})` : '';
};

// Shared pins only obey the text search (name / server source) — the page-image kind /
// format / size filters don't apply to a remote project. No search = always shown.
const sharedMatchesSearch = (image, search) => {
  if (!search) return true;
  const q = search.toLowerCase();
  return (image.name || '').toLowerCase().includes(q) || (image.source || '').toLowerCase().includes(q);
};

// Persist the FILTER controls (search / formats / sizes / include toggles) so they
// survive the popup closing and reopening (the popup's DOM is rebuilt each open). The
// mark-opened / opened-first / highlight toggles persist on their own elsewhere.
const FILTERS_KEY = 'popupFilters';
let persistedFilters = null;
// JSON of the filter state we last wrote, so the storage.onChanged listener can tell
// our own write from another surface's and skip echoing it back (avoids a loop).
let lastSavedJson = null;
const loadPersistedFilters = async () => {
  try { persistedFilters = (await chrome.storage.local.get(FILTERS_KEY))[FILTERS_KEY] || null; }
  catch { persistedFilters = null; }
  lastSavedJson = persistedFilters ? JSON.stringify(persistedFilters) : null;
};
const saveFilters = () => {
  const f = readFilters();
  persistedFilters = {
    search: f.search, minW: f.minW, maxW: f.maxW, minH: f.minH, maxH: f.maxH,
    includeImg: f.includeImg, includeBg: f.includeBg, includeVideo: f.includeVideo, includePosters: f.includePosters,
    disabledFormats: formatCheckboxes().filter(c => !c.checked).map(c => c.value),   // store the OFF ones (new formats default on)
  };
  lastSavedJson = JSON.stringify(persistedFilters);
  try { chrome.storage.local.set({ [FILTERS_KEY]: persistedFilters }); } catch { /* storage unavailable */ }
};
// Restore the static controls from persisted state (format checkboxes are restored in
// populateFormats, since they're rebuilt on every scan).
const restoreStaticFilters = () => {
  if (!persistedFilters) return;
  const f = persistedFilters;
  const setV = (id, v) => { const el = document.getElementById(id); if (el) el.value = v == null ? '' : v; };
  setV('f-search', f.search); setV('f-minw', f.minW); setV('f-maxw', f.maxW); setV('f-minh', f.minH); setV('f-maxh', f.maxH);
  const setC = (id, v) => { const el = document.getElementById(id); if (el && typeof v === 'boolean') el.checked = v; };
  setC('f-img', f.includeImg); setC('f-bg', f.includeBg); setC('f-video', f.includeVideo); setC('f-poster', f.includePosters);
};

const applyFilters = () => {
  filters = readFilters();
  saveFilters();                         // persist the current filter state on every change
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
  let sharedRows = state.shared.filter(s => sharedMatchesSearch(s, filters.search));
  if (!state.showServerPins) sharedRows = [];
  else if (store !== 'all') sharedRows = sharedRows.filter(s => s.serverUrl === store);
  state.filtered = state.filtered.concat(sharedRows);
  listEl.innerHTML = '';
  renderCount();
  if (!state.all.length && !sharedRows.length) {
    listEl.innerHTML = '<li class="empty">No images found on this page.</li>';
    return;
  }
  if (!state.filtered.length) {
    listEl.innerHTML = '<li class="empty">No images match the filters.</li>';
    return;
  }
  statusEl.textContent = '';
  // Render every matching row; thumbnails + size measurement load lazily on scroll.
  state.filtered.forEach(renderRow);
};

// A row that represents a project (a shared server-project row, or a local pin with kind
// 'project') — only these recolour their name; plain page images/pins keep the theme colour.
const isProjectRow = (image) => !!image.shared || image.kind === 'project';

// ── Rows ──
const renderRow = (image) => {
  const li = document.createElement('li');
  const row = document.createElement('div');
  row.className = 'row';

  // Provenance for the name's tooltip: the gestures and where the image came from. An
  // embedded data: URI is a huge base64 blob — show just its short mime prefix, never
  // the whole thing (it would otherwise fill the screen, as the native title did).
  const kindLabel = { bg: 'Background image', video: 'Video', img: 'Image' }[image.kind] || 'Image';
  const refOf = (src) => src && src.startsWith('data:')
    ? src.slice(0, src.indexOf(',') + 1 || 32) + '…'   // e.g. "data:image/jpeg;base64,…"
    : src;
  const ref = image.kind === 'video' ? (image.videoUrl || '(in-page video)') : refOf(image.src);
  const hint = image.kind === 'video'
    ? (image.src ? 'Click: open current frame · Double-click: crop frame' : 'Use the ⋯ menu to open the video')
    : 'Click: open in editor · Double-click: quick crop';
  const title = `${ref}\n\n${kindLabel}\n${hint}`;

  const thumb = document.createElement('img');
  thumb.className = 'thumb';
  // Only assign a real src — a shared (server) row has none here (its bytes are fetched
  // authed below), and assigning '' would point the <img> at the page document.
  const initSrc = image.src || image.posterUrl || (image.kind === 'video' ? PLAY_THUMB : '');
  if (initSrc) thumb.src = initSrc;
  thumb.loading = 'lazy';
  // No native title on the thumbnail: hovering shows the floating preview (bindPreview),
  // which a tooltip would cover; the same info stays on the name's title.
  // Video with no readable frame → play glyph. Any other broken thumb is likely a
  // hotlink-protected source a bare <img> can't load — retry once via fetchAsDataUrl
  // (host permissions), reusing the preview cache; hide only if that fails too.
  thumb.addEventListener('error', async () => {
    if (image.kind === 'video') { thumb.src = PLAY_THUMB; return; }
    const src = editableSrc(image);
    if (thumb.dataset.recovered || !src || src.startsWith('data:')) { thumb.style.visibility = 'hidden'; return; }
    thumb.dataset.recovered = '1';
    try {
      const dataUrl = previewCache.get(src) || await fetchAsDataUrl(src);
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
  name.title = title;
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
  const badge = document.createElement('span');
  badge.className = 'badge ' + image.kind;   // .bg / .video styled; .img neutral
  badge.textContent = image.kind;
  sub.appendChild(badge);
  // A video poster lists as a normal image; tag it so it's distinct from the
  // sibling video (frame) row it was split from.
  if (image.poster) {
    const pb = document.createElement('span');
    pb.className = 'badge poster';
    pb.textContent = 'poster';
    pb.title = 'A video’s preview (poster) image — independent of its frames';
    sub.appendChild(pb);
  }
  const f = document.createElement('span');
  f.className = 'badge fmt';
  // A video shows its container format (from the media URL), not the frame's jpg;
  // in-page (blob) videos fall back to a plain "video" tag.
  f.textContent = image.kind === 'video'
    ? (formatOf(image.videoUrl) || 'video')
    : (formatOf(image.src) || UNKNOWN_FORMAT);
  sub.appendChild(f);
  const dim = document.createElement('span');
  dim.className = 'dim';
  dim.textContent = image.w && image.h ? `${image.w}×${image.h}` : '';
  sub.appendChild(dim);
  // Pin status is conveyed by the row OUTLINE COLOUR alone (no text badge): GOLD = stored
  // on a connected server (a shared row, or a local pin whose image is also on a server,
  // matched by source URL); GRAY = pinned locally only.
  const onServer = (state.showServerPins !== false)
    && (image.shared || (state.sharedSources && state.sharedSources.has(sourceOf(image))));
  if (onServer) row.classList.add('shared');
  else if (isPinned(image)) row.classList.add('pinned');
  // Already opened in an editor: a yellow outline + flag. Clicking the row opens
  // the resume/copy chooser (see bindRowGestures / buildMenu).
  if (isOpened(image)) {
    row.classList.add('opened');
    const ob = document.createElement('span');
    ob.className = 'badge opened';
    ob.innerHTML = icon('flag', { size: 12 }) + ' opened';
    ob.title = 'Already opened in an editor — click to resume or add a copy';
    sub.appendChild(ob);
  }
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
      else pinWithPrompt(image);
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
    dimEl.textContent = image.w && image.h ? `${image.w}×${image.h}` : '';
    // The now-known size may no longer match — drop the row, keep the counter synced.
    if (!passesFilters(image, filters)) {
      li.remove();
      state.filtered = state.filtered.filter(it => it !== image);
      renderCount();
    }
  };
  probe.src = image.src;
};

// ── Floating "…" action menu ──
let menuAnchor = null;

// Fill the menu with the actions for `image`. Editor actions come in pairs: a new tab
// and an in-page modal (▣, mirrors the quick-crop modal), each normal and incognito.
const buildMenu = (image) => {
  menuEl.innerHTML = '';
  const item = (icon, label, fn) => {
    const b = document.createElement('button');
    b.innerHTML = `<span class="ic">${icon}</span>${label}`;
    b.addEventListener('click', async () => {
      closeMenu();
      await run(fn);
    });
    return b;
  };
  const sep = () => {
    const d = document.createElement('div');
    d.className = 'sep';
    return d;
  };
  // Non-clickable sub-category heading.
  const label = (text) => {
    const d = document.createElement('div');
    d.className = 'label';
    d.textContent = text;
    return d;
  };
  // A nested submenu shown as a flyout on hover. Opens to the right; flips to the left
  // when it would overflow the popup window's right edge (measured on hover).
  const submenu = (iconHtml, labelText, children) => {
    const wrap = document.createElement('div');
    wrap.className = 'submenu';
    const head = document.createElement('button');
    head.className = 'submenu-head';
    head.type = 'button';
    head.innerHTML = `<span class="ic">${iconHtml}</span><span class="submenu-label">${labelText}</span>`
      + `<span class="caret">${icon('chevron-right', { size: 12 })}</span>`;
    const fly = document.createElement('div');
    fly.className = 'flyout';
    for (const c of children) fly.append(c);
    const FLYOUT_W = 176;   // keep in sync with .flyout width in popup.css
    wrap.addEventListener('mouseenter', () => {
      const r = head.getBoundingClientRect();
      wrap.classList.toggle('open-left', r.right + FLYOUT_W > window.innerWidth);
    });
    wrap.append(head, fly);
    return wrap;
  };
  // The requested nested actions: Open ▸ / Crop ▸ / Pin ▸. Reused across image, shared,
  // video-frame and poster contexts.
  const editSub = (img) => submenu(icon('pencil', { size: 15 }), 'Open', [
    item(icon('monitor', { size: 15 }), 'Here', () => sendToEditorModal(img, false)),
    item(icon('external', { size: 15 }), 'In editor', () => sendToEditor(img, false)),
    item(icon('incognito', { size: 15 }), 'In editor (incognito)', () => sendToEditor(img, true)),
  ]);
  const cropSub = (img) => submenu(icon('crop', { size: 15 }), 'Crop', [
    item(icon('crop', { size: 15 }), 'Here', () => openCrop(img)),
    item(icon('pencil', { size: 15 }), 'In editor', () => sendToEditor(img, false)),
    item(icon('incognito', { size: 15 }), 'In editor (incognito)', () => sendToEditor(img, true)),
  ]);
  const pinSub = (img) => submenu(icon('pin', { size: 15 }), img.pinned ? 'Pinned' : 'Pin',
    img.pinned
      ? [item(icon('pin', { size: 15 }), 'Unpin', () => togglePin(img)),
         item(icon('server', { size: 15 }), 'Store on server…', () => pinWithPrompt(img))]
      : [item(icon('pin', { size: 15 }), 'Locally', () => togglePin(img)),
         item(icon('server', { size: 15 }), 'On server…', () => pinWithPrompt(img))]);

  // Already opened: offer to resume the existing editor (switches to the matching
  // project, or lets the user pick when several share this image) or add a fresh
  // numbered copy. Shown first since it's the point of the yellow badge.
  if (isOpened(image)) {
    const n = image.opened.reduce((a, e) => Math.max(a, e.count || 1), 0);
    menuEl.append(
      item(icon('refresh', { size: 15 }), `Resume in editor (opened ${n}×)`, () => sendToEditor(image, false, 'resume')),
      item('＋', 'Add as new copy', () => sendToEditor(image, false, 'copy')),
      sep()
    );
  }
  if (image.shared) {
    // A shared (server) row: open / crop the server-stored image. No download/open-in-tab
    // (bytes behind Bearer auth) and no pin (it's already on the server).
    menuEl.append(label('Shared from server'), editSub(image), cropSub(image));
  } else if (image.kind === 'video') {
    if (image.videoUrl) menuEl.append(
      item(icon('external', { size: 15 }), 'Open video in new tab', () => chrome.tabs.create({ url: image.videoUrl })),
      item(icon('download', { size: 15 }), 'Download video', () => download(image.videoUrl))
    );
    if (editableSrc(image)) {
      if (image.videoUrl) menuEl.append(sep());
      menuEl.append(label('Current frame'), editSub(image), cropSub(image));
    }
    if (image.posterUrl) {
      const poster = posterImage(image);
      menuEl.append(
        sep(), label('Video preview image'),
        item(icon('external', { size: 15 }), 'Open preview in new tab', () => chrome.tabs.create({ url: poster.src })),
        item(icon('download', { size: 15 }), 'Download preview', () => download(poster.src)),
        editSub(poster), cropSub(poster)
      );
    }
    if (pinnable(image)) menuEl.append(sep(), pinSub(image));
  } else {
    menuEl.append(
      item(icon('download', { size: 15 }), 'Download', () => download(image.src)),
      item(icon('external', { size: 15 }), 'Open in new tab', () => chrome.tabs.create({ url: image.src })),
      sep(),
      editSub(image),
      cropSub(image)
    );
    if (pinnable(image)) menuEl.append(pinSub(image));
  }
};

// Place the (already-built, visible) menu at top-left x/y, flipping to stay on-screen.
const placeMenu = (x, y) => {
  const mw = menuEl.offsetWidth;
  const mh = menuEl.offsetHeight;
  if (x + mw > window.innerWidth) x = Math.max(6, window.innerWidth - mw - 6);
  if (y + mh > window.innerHeight) y = Math.max(6, window.innerHeight - mh - 6);
  menuEl.style.left = `${Math.max(6, x)}px`;
  menuEl.style.top = `${Math.max(6, y)}px`;
};

// Open the menu anchored to the ⋯ button (toggles closed if already open on it).
const openMenu = (btn, image) => {
  if (menuAnchor === btn) return closeMenu();
  closeMenu();
  menuAnchor = btn;
  btn.classList.add('active');
  buildMenu(image);
  menuEl.hidden = false;
  // Prefer the left of the button; fall back to its right if it won't fit.
  const r = btn.getBoundingClientRect();
  let x = r.left - menuEl.offsetWidth - 6;
  if (x < 6) x = r.right + 6;
  placeMenu(x, r.top);
};

// Open the same menu at a point (used by row right-click); no button is anchored.
const openMenuAt = (image, x, y) => {
  closeMenu();
  buildMenu(image);
  menuEl.hidden = false;
  placeMenu(x, y);
};
const closeMenu = () => {
  menuEl.hidden = true;
  menuEl.innerHTML = '';
  if (menuAnchor) menuAnchor.classList.remove('active');
  menuAnchor = null;
};
document.addEventListener('click', (e) => { if (!menuEl.contains(e.target)) closeMenu(); });
document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeMenu(); });
listEl.addEventListener('scroll', closeMenu);

const run = async (fn) => {
  try {
    await fn();
  } catch (err) {
    statusEl.textContent = `Failed: ${err.message}`;
  }
};

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

// Single click → open in the editor (normal, not incognito); double click → crop.
const bindOpenGestures = (el, image) =>
  bindGestures(el, () => sendToEditor(image, false), () => openCrop(image));

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
    ? () => sendToEditor(image, false)
    : (image.videoUrl ? () => chrome.tabs.create({ url: image.videoUrl }) : null);
  const onDouble = es ? () => openCrop(image) : null;
  bindGestures(el, onClick, onDouble);
};

// ── Actions ──
const download = (src) => chrome.downloads.download({ url: src, filename: filenameFromUrl(src) });

// Pin / unpin an image on this site, then re-render so it floats (or settles back).
// The storage write also reaches any open side panel / DevTools panel and the page API
// (entry.pinned) via their storage.onChanged listeners.
const setPinnedState = async (image, pinned) => {
  image.pinned = pinned;
  await setPinned({
    source: sourceOf(image), site: siteOf(state.activeUrl), resource: state.activeUrl,
    name: image.name, kind: image.kind, pinned,
  });
  applyFilters();           // re-sorts + re-renders: a pinned row floats to the top
  // Follow the row to its new position so it stays in view (and flash it), instead of it
  // jumping off-screen while the scroll stays put. Rows render in state.filtered order.
  const idx = state.filtered.indexOf(image);
  const row = idx >= 0 ? listEl.children[idx]?.querySelector('.row') : null;
  if (row) {
    row.scrollIntoView({ behavior: 'smooth', block: 'center' });
    row.classList.remove('just-pinned');
    void row.offsetWidth;   // restart the flash animation if it was mid-run
    row.classList.add('just-pinned');
  }
};

// Toggle local pin (used by the row's pin button + the menu's Unpin / "Pin locally").
const togglePin = async (image) => setPinnedState(image, !image.pinned);

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
const pinWithPrompt = async (image) => {
  const target = await promptPinTarget();      // undefined = cancel, '' = local, url = server
  if (target === undefined) return;            // cancelled — don't pin
  if (!image.pinned) await setPinnedState(image, true);
  if (target) await storeOnServer(image, target);
};

// Host label for a server origin (e.g. https://srv:8090 → srv:8090).
const hostLabel = (origin) => {
  try {
    return new URL(origin).host;
  } catch {
    return origin || '(server)';
  }
};

// In-popup dialog asking WHERE to pin: a single SELECTOR (Pin locally / Store on each
// connected server) plus Cancel + Pin. Resolves the chosen server URL, '' for local, or
// undefined when cancelled.
const promptPinTarget = () => new Promise((resolve) => {
  const back = document.createElement('div');
  back.className = 'dialog-back';
  const box = document.createElement('div');
  box.className = 'dialog';
  const finish = (val) => { back.remove(); resolve(val); };

  const title = document.createElement('div');
  title.className = 'dialog-title';
  title.textContent = 'Where do you want to pin this image?';

  const sel = document.createElement('select');
  sel.className = 'dialog-select';
  sel.innerHTML = '<option value="">Pin locally only</option>'
    + state.connections.map((c) => `<option value="${c.url}">Pin & store on ${hostLabel(c.url)}</option>`).join('');

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

  box.append(title, sel, row);
  back.appendChild(box);
  back.addEventListener('click', (e) => { if (e.target === back) finish(undefined); });   // click-away = cancel
  document.addEventListener('keydown', function esc(e) {
    if (e.key !== 'Escape') return;
    document.removeEventListener('keydown', esc);
    finish(undefined);
  });
  document.body.appendChild(back);
});

// Provenance attached to every editor hand-off: the image's own URL (or a video's
// media URL) and the page it was scanned on. `open` ('resume'|'copy') lets the
// editor switch to an already-opened project or force a fresh numbered copy.
const handoff = (image, open) => ({
  name: image.name,
  // A shared (server) row carries the server image URL + its origin page as provenance;
  // a page image uses its own src + the active tab's URL.
  source: image.shared ? image.source : sourceOf(image),
  resource: image.shared ? image.resource : state.activeUrl,
  open
});

// The image bytes to hand to the editor / crop: a shared row pulls them (authed) from
// its server, a page image fetches them through the extension's host permissions.
const imageDataUrl = (image) => image.shared ? sharedDataUrl(image) : fetchAsDataUrl(editableSrc(image));

const sendToEditor = async (image, incognito, open) => {
  statusEl.textContent = 'Loading image…';
  const { page } = await getSettings();
  const dataUrl = await imageDataUrl(image);
  await openEditorTab({ dataUrl, page: { size: page }, incognito, ...handoff(image, open) });
  dismiss();
};

// Same as sendToEditor, but frames the editor in an in-page modal on the active
// page instead of opening a new tab (mirrors the quick-crop modal).
const sendToEditorModal = async (image, incognito, open) => {
  statusEl.textContent = 'Loading image…';
  const { page } = await getSettings();
  const dataUrl = await imageDataUrl(image);
  await launchEditorModal({ dataUrl, page: { size: page }, incognito, tabId: state.activeTabId, ...handoff(image, open) });
  dismiss();
};

// Crop opens a small in-page modal on the current page (full editor stays a tab).
const openCrop = async (image) => {
  const src = image.shared ? await sharedDataUrl(image) : editableSrc(image);
  const h = handoff(image);
  await launchCrop({ src, source: h.source, resource: h.resource, tabId: state.activeTabId });
  dismiss();
};

// ── Hover preview ──
// Skip the floating preview when the image is no bigger than its row thumbnail
// (nothing larger to reveal). Unmeasured images (w/h = 0) still get a preview.
const previewWorthwhile = (image) =>
  !(image.w > 0 && image.h > 0 && image.w <= THUMB_PX && image.h <= THUMB_PX);

// Cache of source → data URL for previews already fetched, so re-hovering a row
// is instant and each source is fetched at most once.
const previewCache = new Map();
// The row the preview is anchored to (to reposition once the image's real size
// is known) and a token to drop a stale async fetch when the pointer moves on
// before fetchAsDataUrl resolves.
let previewAnchor = null;
let previewToken = 0;

const positionPreview = (el) => {
  const r = el.getBoundingClientRect();
  const pw = previewEl.offsetWidth || 270;
  const ph = previewEl.offsetHeight || 270;
  let x = r.right + 12;
  if (x + pw > window.innerWidth) x = Math.max(12, r.left - pw - 12);
  let y = r.top;
  if (y + ph > window.innerHeight) y = Math.max(12, window.innerHeight - ph - 12);
  previewEl.style.left = `${x}px`;
  previewEl.style.top = `${y}px`;
};

// Resolve a source to something the popup <img> can display. A bare <img src> can't
// load a hotlink-protected / cross-origin poster (no referrer/cookies) — it renders a
// void box; fetchAsDataUrl pulls it through the extension's host permissions instead.
const resolvePreviewSrc = async (src) => {
  if (src.startsWith('data:')) return src;
  if (previewCache.has(src)) return previewCache.get(src);
  const dataUrl = await fetchAsDataUrl(src);
  previewCache.set(src, dataUrl);
  return dataUrl;
};

// Reposition once the real dimensions are known; hide (rather than leave a void
// box) if even the fetched data URL won't decode.
previewImg.addEventListener('load', () => {
  if (previewAnchor) positionPreview(previewAnchor);
});
previewImg.addEventListener('error', () => { previewEl.hidden = true; });

const bindPreview = (el, image) => {
  el.addEventListener('mouseenter', async () => {
    const ps = editableSrc(image);
    if (!ps || !previewWorthwhile(image)) return;   // nothing to preview
    const token = ++previewToken;
    previewAnchor = el;
    let src = ps;
    try {
      src = await resolvePreviewSrc(ps);
    } catch {
      src = ps;   // fall back to a direct load; the error handler hides a void box
    }
    if (token !== previewToken) return;   // pointer already moved on
    previewImg.src = src;
    previewEl.hidden = false;
    positionPreview(el);
  });
  el.addEventListener('mouseleave', () => {
    previewToken++;       // cancel any in-flight fetch for this row
    previewAnchor = null;
    previewEl.hidden = true;
  });
};

// ── Wiring ──
let searchTimer = null;
document.getElementById('f-search').addEventListener('input', () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(applyFilters, 150);
});
['f-img', 'f-bg', 'f-video', 'f-poster'].forEach(id => document.getElementById(id).addEventListener('change', applyFilters));

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

// The on-page highlight colour: the main accent ('theme') or a custom hex (options).
// The accent key comes from this page's StencilAccent (localStorage); resolve to a hex.
const highlightColorValue = async () => {
  const { highlightColor } = await getSettings();
  let accentKey = 'violet';
  try { accentKey = window.StencilAccent.get(); } catch { /* default */ }
  return resolveHighlightColor(highlightColor, accentKey);
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
  const target = !allFormatsChecked();
  formatCheckboxes().forEach(c => { c.checked = target; });
  updateToggleLabel();
  applyFilters();
});
['f-minw', 'f-maxw', 'f-minh', 'f-maxh'].forEach(id => document.getElementById(id).addEventListener('input', () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(applyFilters, 150);
}));
document.getElementById('rescan').addEventListener('click', scan);
document.getElementById('open-options').addEventListener('click', () => chrome.runtime.openOptionsPage());

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
    const nv = changes[FILTERS_KEY].newValue || null;
    if (nv && JSON.stringify(nv) !== lastSavedJson) {
      persistedFilters = nv;
      lastSavedJson = JSON.stringify(nv);
      restoreStaticFilters();      // search / sizes / kind toggles
      applyPersistedFormats();     // format checkboxes (already rendered)
      applyFilters();              // re-filter the list to match
    }
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
});

// Load the persisted filters first, restore the static controls, then scan (populateFormats
// restores the format toggles from the same persisted state).
loadPersistedFilters().then(() => { restoreStaticFilters(); scan(); });
