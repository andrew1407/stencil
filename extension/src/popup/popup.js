// ── Popup: list, filter, and act on every image on the active page ───────────
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchCrop, getSettings } from '../lib/stencil.js';
import { scanPageForImages } from '../lib/imageScan.js';
import { toggleStencilHighlight } from '../lib/highlight.js';
import { passesFilters, distinctFormats, formatOf, UNKNOWN_FORMAT } from '../lib/filters.js';

// Common web image formats always offered in the filter, plus any others the
// page actually uses (added in populateFormats).
const COMMON_FORMATS = ['png', 'jpg', 'gif', 'webp', 'svg', 'avif', 'bmp', 'ico', 'tiff'];

const listEl = document.getElementById('list');
const statusEl = document.getElementById('status');
const countEl = document.getElementById('count');
const previewEl = document.getElementById('preview');
const previewImg = previewEl.querySelector('img');
const menuEl = document.getElementById('action-menu');

const MAX_IMAGES = 1000; // hard cap on what we pull from the page
const THUMB_PX = 48;     // rendered thumbnail size (see .thumb in popup.css)
// Placeholder thumbnail for a video whose frame couldn't be read (cross-origin).
const PLAY_THUMB = 'data:image/svg+xml,' + encodeURIComponent(
  '<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48"><rect width="48" height="48" fill="#2b2f3a"/><polygon points="19,15 35,24 19,33" fill="#7c3aed"/></svg>');

const state = { all: [], filtered: [], activeTabId: null };

// Lazily measure unknown-size images only once their row scrolls into view —
// the actual lazy-on-scroll behaviour (thumbnails use loading="lazy" too). All
// matching rows are rendered up front so search/filtering shows everything.
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
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  if (!tab || /^(chrome|edge|about|chrome-extension|view-source):/.test(tab.url || '')) {
    statusEl.textContent = 'This page can’t be scanned.';
    return;
  }
  state.activeTabId = tab.id;
  await syncHighlightCheckbox(tab.id);
  let images = [];
  try {
    // Scan every frame (sites often put the real content in an iframe), then
    // merge + dedupe across frames.
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
  populateFormats();
  applyFilters();
};

// Build a checkbox per format (common ones + any extra the page uses). All start
// checked (= no filtering); the toggle button flips select-all / deselect-all.
const populateFormats = () => {
  const present = new Set(distinctFormats(state.all));
  // 'etc' covers images with no detectable format; always offered, marked
  // present only when the page actually has such images. Kept last in the list.
  if (state.all.some(it => !formatOf(it.src))) present.add(UNKNOWN_FORMAT);
  const extras = [...present].filter(f => !COMMON_FORMATS.includes(f) && f !== UNKNOWN_FORMAT);
  const formats = [...COMMON_FORMATS, ...extras, UNKNOWN_FORMAT];
  const box = document.getElementById('f-formats');
  box.innerHTML = formats.map(f =>
    `<label class="${present.has(f) ? '' : 'absent'}"><input type="checkbox" value="${f}" checked>${f.toUpperCase()}</label>`
  ).join('');
  box.querySelectorAll('input').forEach(cb =>
    cb.addEventListener('change', () => {
      updateToggleLabel();
      applyFilters();
    }));
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
    includeVideo: document.getElementById('f-video').checked
  };
};

let filters = {};
const renderCount = () => {
  countEl.textContent = state.all.length ? `(${state.filtered.length}/${state.all.length})` : '';
};

const applyFilters = () => {
  filters = readFilters();
  state.filtered = state.all.filter(it => passesFilters(it, filters));
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

  // Tooltip on both the thumbnail and the name, spelling out the gestures and
  // where the image came from (<img>, a CSS background, or a video).
  const kindLabel = { bg: 'Background image', video: 'Video', img: 'Image' }[image.kind] || 'Image';
  const ref = image.kind === 'video' ? (image.videoUrl || '(in-page video)') : image.src;
  const hint = image.kind === 'video'
    ? (image.src ? 'Click: open current frame · Double-click: crop frame' : 'Use the ⋯ menu to open the video')
    : 'Click: open in editor · Double-click: quick crop';
  const title = `${ref}\n\n${kindLabel}\n${hint}`;

  const thumb = document.createElement('img');
  thumb.className = 'thumb';
  thumb.src = image.src || (image.kind === 'video' ? PLAY_THUMB : '');
  thumb.loading = 'lazy';
  thumb.title = title;
  // A video with no readable frame shows a play glyph; any other broken thumb hides.
  thumb.addEventListener('error', () => {
    if (image.kind === 'video') thumb.src = PLAY_THUMB;
    else thumb.style.visibility = 'hidden';
  });
  bindPreview(thumb, image);

  const meta = document.createElement('div');
  meta.className = 'meta';
  const name = document.createElement('div');
  name.className = 'name clickable';
  name.textContent = image.name;
  name.title = title;

  // Click the thumbnail or name → open in editor (asks about incognito);
  // double-click → quick crop. Disambiguated with a short timer. Videos open/crop
  // their current frame, or open the video in a tab when no frame is available.
  bindRowGestures(thumb, image);
  bindRowGestures(name, image);
  const sub = document.createElement('div');
  sub.className = 'sub';
  const badge = document.createElement('span');
  badge.className = 'badge ' + image.kind;   // .bg / .video styled; .img neutral
  badge.textContent = image.kind;
  sub.appendChild(badge);
  const f = document.createElement('span');
  f.className = 'badge fmt';
  // For a video show its container format (mp4/webm…) from the media URL, not the
  // frame's jpg; fall back to a plain "video" tag for in-page (blob) videos.
  f.textContent = image.kind === 'video'
    ? (formatOf(image.videoUrl) || 'video')
    : (formatOf(image.src) || UNKNOWN_FORMAT);
  sub.appendChild(f);
  const dim = document.createElement('span');
  dim.className = 'dim';
  dim.textContent = image.w && image.h ? `${image.w}×${image.h}` : '';
  sub.appendChild(dim);
  meta.append(name, sub);

  const more = document.createElement('button');
  more.className = 'more-btn';
  more.textContent = '⋯';
  more.title = 'Actions';
  more.addEventListener('click', (e) => {
    e.stopPropagation();
    openMenu(more, image);
  });

  row.append(thumb, meta, more);
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
    // Now that the real size is known the item may no longer match — drop the row
    // and keep the (visible/total) counter in sync.
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
const openMenu = (btn, image) => {
  if (menuAnchor === btn) return closeMenu();
  closeMenu();
  menuAnchor = btn;
  btn.classList.add('active');
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
  if (image.kind === 'video') {
    if (image.videoUrl) menuEl.append(
      item('↗', 'Open video in new tab', () => chrome.tabs.create({ url: image.videoUrl })),
      item('⬇', 'Download video', () => download(image.videoUrl))
    );
    if (image.src) {
      if (image.videoUrl) menuEl.append(sep());
      menuEl.append(
        item('✎', 'Open current frame in editor', () => sendToEditor(image.src, false)),
        item('🕶', 'Frame in editor (incognito)', () => sendToEditor(image.src, true)),
        item('✂', 'Crop current frame', () => openCrop(image.src))
      );
    }
  } else {
    menuEl.append(
      item('⬇', 'Download', () => download(image.src)),
      item('↗', 'Open in new tab', () => chrome.tabs.create({ url: image.src })),
      sep(),
      item('✎', 'Open in editor', () => sendToEditor(image.src, false)),
      item('🕶', 'Editor (incognito)', () => sendToEditor(image.src, true)),
      item('✂', 'Crop…', () => openCrop(image.src))
    );
  }
  menuEl.hidden = false;
  // Position next to the button, flipping to stay on-screen.
  const r = btn.getBoundingClientRect();
  const mw = menuEl.offsetWidth;
  const mh = menuEl.offsetHeight;
  let x = r.left - mw - 6;
  if (x < 6) x = Math.min(r.right + 6, window.innerWidth - mw - 6);
  let y = r.top;
  if (y + mh > window.innerHeight) y = Math.max(6, window.innerHeight - mh - 6);
  menuEl.style.left = `${x}px`;
  menuEl.style.top = `${y}px`;
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
// Click → editor, double-click → crop, disambiguated with a short timer. The
// click/dblclick actions are passed in so videos can act on their frame (or open
// the video in a tab when there's no frame).
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

const bindOpenGestures = (el, src) =>
  bindGestures(el, () => openEditorWithConfirm(src), () => openCrop(src));

const bindRowGestures = (el, image) => {
  if (image.kind !== 'video') return bindOpenGestures(el, image.src);
  // Video: act on the current frame; with no frame, single-click opens the video.
  const onClick = image.src
    ? () => openEditorWithConfirm(image.src)
    : (image.videoUrl ? () => chrome.tabs.create({ url: image.videoUrl }) : null);
  const onDouble = image.src ? () => openCrop(image.src) : null;
  bindGestures(el, onClick, onDouble);
};

// ── Actions ──
const download = (src) => chrome.downloads.download({ url: src, filename: filenameFromUrl(src) });

// Single-click open: confirm incognito vs normal, then open in an editor tab.
const openEditorWithConfirm = (src) => {
  const incognito = confirm(
    'Open this image in the Stencil editor?\n\nOK = incognito mode (won’t be saved)\nCancel = normal editor');
  return sendToEditor(src, incognito);
};

const sendToEditor = async (src, incognito) => {
  statusEl.textContent = 'Loading image…';
  const { page } = await getSettings();
  const dataUrl = await fetchAsDataUrl(src);
  await openEditorTab({ dataUrl, name: filenameFromUrl(src), page: { size: page }, incognito });
  window.close();
};

// Crop opens a small in-page modal on the current page (full editor stays a tab).
const openCrop = async (src) => {
  await launchCrop({ src, tabId: state.activeTabId });
  window.close();
};

// ── Hover preview ──
// Skip the floating preview when the image isn't actually bigger than the
// thumbnail already shown in the row — there'd be nothing larger to reveal.
// Unknown-size images (not yet measured, w/h = 0) still get a preview.
const previewWorthwhile = (image) =>
  !(image.w > 0 && image.h > 0 && image.w <= THUMB_PX && image.h <= THUMB_PX);

const bindPreview = (el, image) => {
  const position = () => {
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
  el.addEventListener('mouseenter', () => {
    if (!image.src || !previewWorthwhile(image)) return;   // nothing to preview (e.g. a frameless video)
    previewImg.src = image.src;
    previewEl.hidden = false;
    position();
  });
  el.addEventListener('mouseleave', () => { previewEl.hidden = true; });
};

// ── Wiring ──
let searchTimer = null;
document.getElementById('f-search').addEventListener('input', () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(applyFilters, 150);
});
['f-img', 'f-bg', 'f-video'].forEach(id => document.getElementById(id).addEventListener('change', applyFilters));

// The highlight lives on the page (it survives the popup closing), so on open
// reflect its real state in the checkbox instead of defaulting to unchecked —
// otherwise reopening the popup would wrongly show it off after you'd turned it on.
const syncHighlightCheckbox = async (tabId) => {
  try {
    const [res] = await chrome.scripting.executeScript({
      target: { tabId }, func: () => !!document.getElementById('stencil-hl-style')
    });
    document.getElementById('f-highlight').checked = !!res?.result;
  } catch { /* restricted page — leave as-is */ }
};

// Highlight toggle: outline every <img> / background-image element on the page
// so the user can see what Stencil can grab. Off by default; injects into the page.
document.getElementById('f-highlight').addEventListener('change', async (e) => {
  if (state.activeTabId == null) { e.target.checked = false; return; }
  try {
    // All frames, so iframed content is highlighted too.
    await chrome.scripting.executeScript({
      target: { tabId: state.activeTabId, allFrames: true }, func: toggleStencilHighlight, args: [e.target.checked]
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

scan();
