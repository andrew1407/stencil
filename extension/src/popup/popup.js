// ── Popup: list, filter, and act on every image on the active page ───────────
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchEditorModal, launchCrop, getSettings, setSettings } from '../lib/stencil.js';
import { LEDGER_KEY, loadLedger, matchEntries, trackableSource } from '../lib/ledger.js';
import { PINS_KEY, loadPins, isPinnedIn, siteOf, setPinned } from '../lib/pins.js';
import { resolveHighlightColor } from '../lib/highlightColor.js';
import { scanPageForImages } from '../lib/imageScan.js';
import { toggleStencilHighlight } from '../lib/highlight.js';
import { icon } from '../lib/icons.js';
import { passesFilters, distinctFormats, formatOf, formatOfItem, UNKNOWN_FORMAT, VIDEO_FORMATS } from '../lib/filters.js';

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

const state = { all: [], filtered: [], activeTabId: null, activeUrl: '', markOpened: true, openedFirst: true, showPinned: true };

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
  countEl.textContent = state.all.length ? `(${state.filtered.length}/${state.all.length})` : '';
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
  listEl.innerHTML = '';
  renderCount();
  if (!state.all.length) {
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
  thumb.src = image.src || image.posterUrl || (image.kind === 'video' ? PLAY_THUMB : '');
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

  const meta = document.createElement('div');
  meta.className = 'meta';
  const name = document.createElement('div');
  name.className = 'name clickable';
  name.textContent = image.name;
  name.title = title;

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
  // Pinned on this site: a gray outline + pin tag, floated to the top. Independent of
  // the opened cue below (an image can be both).
  if (isPinned(image)) {
    row.classList.add('pinned');
    const pb = document.createElement('span');
    pb.className = 'badge pinned';
    pb.innerHTML = icon('pin', { size: 12 }) + ' pinned';
    pb.title = 'Pinned on this site';
    sub.appendChild(pb);
  }
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
      togglePin(image);
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
  // Non-clickable sub-category heading (the flat menu's stand-in for a nested submenu).
  const label = (text) => {
    const d = document.createElement('div');
    d.className = 'label';
    d.textContent = text;
    return d;
  };
  // Pin / unpin on the current site (mirrors the row's pin button).
  if (pinnable(image)) {
    menuEl.append(
      item(icon('pin', { size: 15 }), image.pinned ? 'Unpin' : 'Pin to top', () => togglePin(image)),
      sep()
    );
  }
  // Already opened: offer to resume the existing editor (switches to the matching
  // project, or lets the user pick when several share this image) or add a fresh
  // numbered copy. Shown first since it's the point of the yellow badge.
  if (isOpened(image)) {
    // After reconciliation each matched entry carries the live project count for the
    // source, so they agree — take the max (not a sum, which would multiply duplicates).
    const n = image.opened.reduce((a, e) => Math.max(a, e.count || 1), 0);
    menuEl.append(
      item(icon('refresh', { size: 15 }), `Resume in editor (opened ${n}×)`, () => sendToEditor(image, false, 'resume')),
      item('＋', 'Add as new copy', () => sendToEditor(image, false, 'copy')),
      sep()
    );
  }
  if (image.kind === 'video') {
    if (image.videoUrl) menuEl.append(
      item(icon('external', { size: 15 }), 'Open video in new tab', () => chrome.tabs.create({ url: image.videoUrl })),
      item(icon('download', { size: 15 }), 'Download video', () => download(image.videoUrl))
    );
    if (editableSrc(image)) {
      // With no decoded frame (unplayed video) these act on the poster instead of a
      // black frame, via editableSrc() in sendToEditor / openCrop.
      if (image.videoUrl) menuEl.append(sep());
      menuEl.append(
        item(icon('pencil', { size: 15 }), 'Open current frame in editor', () => sendToEditor(image, false)),
        item(icon('incognito', { size: 15 }), 'Frame in editor (incognito)', () => sendToEditor(image, true)),
        item(icon('monitor', { size: 15 }), 'Open frame in editor here', () => sendToEditorModal(image, false)),
        item(icon('monitor', { size: 15 }), 'Frame in editor here (incognito)', () => sendToEditorModal(image, true)),
        item(icon('crop', { size: 15 }), 'Crop current frame', () => openCrop(image))
      );
    }
    // The poster (preview cover) is a normal image independent of the frames — offer
    // the same open / view / crop actions as the page menu's "Video preview image"
    // submenu, acting on a synthetic image item for the poster URL.
    if (image.posterUrl) {
      const poster = posterImage(image);
      menuEl.append(
        sep(),
        label('Video preview image'),
        item(icon('external', { size: 15 }), 'Open preview in new tab', () => chrome.tabs.create({ url: poster.src })),
        item(icon('download', { size: 15 }), 'Download preview', () => download(poster.src)),
        item(icon('pencil', { size: 15 }), 'Open preview in editor', () => sendToEditor(poster, false)),
        item(icon('incognito', { size: 15 }), 'Preview in editor (incognito)', () => sendToEditor(poster, true)),
        item(icon('monitor', { size: 15 }), 'Open preview in editor here', () => sendToEditorModal(poster, false)),
        item(icon('monitor', { size: 15 }), 'Preview here (incognito)', () => sendToEditorModal(poster, true)),
        item(icon('crop', { size: 15 }), 'Crop preview…', () => openCrop(poster))
      );
    }
  } else {
    menuEl.append(
      item(icon('download', { size: 15 }), 'Download', () => download(image.src)),
      item(icon('external', { size: 15 }), 'Open in new tab', () => chrome.tabs.create({ url: image.src })),
      sep(),
      item(icon('pencil', { size: 15 }), 'Open in editor', () => sendToEditor(image, false)),
      item(icon('incognito', { size: 15 }), 'Editor (incognito)', () => sendToEditor(image, true)),
      item(icon('monitor', { size: 15 }), 'Open in editor here', () => sendToEditorModal(image, false)),
      item(icon('monitor', { size: 15 }), 'Editor here (incognito)', () => sendToEditorModal(image, true)),
      item(icon('crop', { size: 15 }), 'Crop…', () => openCrop(image))
    );
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
const togglePin = async (image) => {
  const next = !image.pinned;
  image.pinned = next;
  await setPinned({
    source: sourceOf(image), site: siteOf(state.activeUrl), resource: state.activeUrl,
    name: image.name, kind: image.kind, pinned: next,
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

// Provenance attached to every editor hand-off: the image's own URL (or a video's
// media URL) and the page it was scanned on. `open` ('resume'|'copy') lets the
// editor switch to an already-opened project or force a fresh numbered copy.
const handoff = (image, open) => ({
  name: image.name,
  source: sourceOf(image),
  resource: state.activeUrl,
  open
});

const sendToEditor = async (image, incognito, open) => {
  statusEl.textContent = 'Loading image…';
  const { page } = await getSettings();
  const dataUrl = await fetchAsDataUrl(editableSrc(image));
  await openEditorTab({ dataUrl, page: { size: page }, incognito, ...handoff(image, open) });
  dismiss();
};

// Same as sendToEditor, but frames the editor in an in-page modal on the active
// page instead of opening a new tab (mirrors the quick-crop modal).
const sendToEditorModal = async (image, incognito, open) => {
  statusEl.textContent = 'Loading image…';
  const { page } = await getSettings();
  const dataUrl = await fetchAsDataUrl(editableSrc(image));
  await launchEditorModal({ dataUrl, page: { size: page }, incognito, tabId: state.activeTabId, ...handoff(image, open) });
  dismiss();
};

// Crop opens a small in-page modal on the current page (full editor stays a tab).
const openCrop = async (image) => {
  await launchCrop({ src: editableSrc(image), source: sourceOf(image), resource: state.activeUrl, tabId: state.activeTabId });
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
