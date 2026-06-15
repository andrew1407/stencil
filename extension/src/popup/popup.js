// ── Popup: list, filter, and act on every image on the active page ───────────
import { fetchAsDataUrl, filenameFromUrl, openEditorTab, launchCrop, getSettings } from '../lib/stencil.js';
import { scanPageForImages } from '../lib/imageScan.js';
import { passesFilters, distinctFormats, formatOf } from '../lib/filters.js';

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
async function scan() {
  listEl.innerHTML = ''; statusEl.textContent = 'Scanning…';
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  if (!tab || /^(chrome|edge|about|chrome-extension|view-source):/.test(tab.url || '')) {
    statusEl.textContent = 'This page can’t be scanned.'; return;
  }
  state.activeTabId = tab.id;
  let images = [];
  try {
    const [res] = await chrome.scripting.executeScript({
      target: { tabId: tab.id, allFrames: false }, func: scanPageForImages, args: [MAX_IMAGES]
    });
    images = res?.result || [];
  } catch (err) {
    statusEl.textContent = `Could not read this page (${err.message}).`; return;
  }
  state.all = images.map(it => ({ ...it, name: filenameFromUrl(it.src), measured: it.w > 0 && it.h > 0 }));
  populateFormats();
  applyFilters();
}

// Build a checkbox per format (common ones + any extra the page uses). All start
// checked (= no filtering); the toggle button flips select-all / deselect-all.
function populateFormats() {
  const present = new Set(distinctFormats(state.all));
  const formats = [...COMMON_FORMATS, ...[...present].filter(f => !COMMON_FORMATS.includes(f))];
  const box = document.getElementById('f-formats');
  box.innerHTML = formats.map(f =>
    `<label class="${present.has(f) ? '' : 'absent'}"><input type="checkbox" value="${f}" checked>${f.toUpperCase()}</label>`
  ).join('');
  box.querySelectorAll('input').forEach(cb =>
    cb.addEventListener('change', () => { updateToggleLabel(); applyFilters(); }));
  updateToggleLabel();
}

function formatCheckboxes() { return [...document.getElementById('f-formats').querySelectorAll('input')]; }
function allFormatsChecked() {
  const cbs = formatCheckboxes();
  return cbs.length > 0 && cbs.every(c => c.checked);
}
function updateToggleLabel() { document.getElementById('f-fmt-toggle').textContent = allFormatsChecked() ? 'Deselect all' : 'Select all'; }

// ── Filtering ──
function readFilters() {
  const num = el => {
    const v = parseFloat(el.value);
    return isNaN(v) ? null : v;
  };
  return {
    search: document.getElementById('f-search').value.trim(),
    formats: formatCheckboxes().filter(c => c.checked).map(c => c.value),
    minW: num(document.getElementById('f-minw')), maxW: num(document.getElementById('f-maxw')),
    minH: num(document.getElementById('f-minh')), maxH: num(document.getElementById('f-maxh')),
    includeImg: document.getElementById('f-img').checked, includeBg: document.getElementById('f-bg').checked
  };
}

let filters = {};
function renderCount() {
  countEl.textContent = state.all.length ? `(${state.filtered.length}/${state.all.length})` : '';
}

function applyFilters() {
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
}

// ── Rows ──
function renderRow(image) {
  const li = document.createElement('li');
  const row = document.createElement('div');
  row.className = 'row';

  const thumb = document.createElement('img');
  thumb.className = 'thumb'; thumb.src = image.src; thumb.loading = 'lazy';
  thumb.addEventListener('error', () => { thumb.style.visibility = 'hidden'; });
  bindPreview(thumb, image.src);

  const meta = document.createElement('div'); meta.className = 'meta';
  const name = document.createElement('div'); name.className = 'name clickable';
  name.textContent = image.name;
  name.title = `${image.src}\n\nClick: open in editor · Double-click: quick crop`;

  // Click the thumbnail or name → open in editor (asks about incognito);
  // double-click → quick crop. Disambiguated with a short timer.
  bindOpenGestures(thumb, image.src);
  bindOpenGestures(name, image.src);
  const sub = document.createElement('div'); sub.className = 'sub';
  const badge = document.createElement('span');
  badge.className = 'badge' + (image.kind === 'bg' ? ' bg' : '');
  badge.textContent = image.kind === 'bg' ? 'bg' : 'img';
  const fmt = formatOf(image.src);
  sub.appendChild(badge);
  if (fmt) {
    const f = document.createElement('span');
    f.className = 'badge fmt';
    f.textContent = fmt;
    sub.appendChild(f);
  }
  const dim = document.createElement('span'); dim.className = 'dim';
  dim.textContent = image.w && image.h ? `${image.w}×${image.h}` : '';
  sub.appendChild(dim);
  meta.append(name, sub);

  const more = document.createElement('button');
  more.className = 'more-btn'; more.textContent = '⋯'; more.title = 'Actions';
  more.addEventListener('click', (e) => { e.stopPropagation(); openMenu(more, image); });

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
}

function measure(image, dimEl, li) {
  const probe = new Image();
  probe.onload = () => {
    image.w = probe.naturalWidth; image.h = probe.naturalHeight; image.measured = true;
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
}

// ── Floating "…" action menu ──
let menuAnchor = null;
function openMenu(btn, image) {
  if (menuAnchor === btn) return closeMenu();
  closeMenu();
  menuAnchor = btn; btn.classList.add('active');
  menuEl.innerHTML = '';
  const item = (icon, label, fn) => {
    const b = document.createElement('button');
    b.innerHTML = `<span class="ic">${icon}</span>${label}`;
    b.addEventListener('click', async () => { closeMenu(); await run(fn); });
    return b;
  };
  const sep = () => {
    const d = document.createElement('div');
    d.className = 'sep';
    return d;
  };
  menuEl.append(
    item('⬇', 'Download', () => download(image.src)),
    item('↗', 'Open in new tab', () => chrome.tabs.create({ url: image.src })),
    sep(),
    item('✎', 'Open in editor', () => sendToEditor(image.src, false)),
    item('🕶', 'Editor (incognito)', () => sendToEditor(image.src, true)),
    item('✂', 'Crop…', () => openCrop(image.src))
  );
  menuEl.hidden = false;
  // Position next to the button, flipping to stay on-screen.
  const r = btn.getBoundingClientRect();
  const mw = menuEl.offsetWidth, mh = menuEl.offsetHeight;
  let x = r.left - mw - 6; if (x < 6) x = Math.min(r.right + 6, window.innerWidth - mw - 6);
  let y = r.top; if (y + mh > window.innerHeight) y = Math.max(6, window.innerHeight - mh - 6);
  menuEl.style.left = `${x}px`; menuEl.style.top = `${y}px`;
}
function closeMenu() {
  menuEl.hidden = true; menuEl.innerHTML = '';
  if (menuAnchor) menuAnchor.classList.remove('active');
  menuAnchor = null;
}
document.addEventListener('click', (e) => { if (!menuEl.contains(e.target)) closeMenu(); });
document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeMenu(); });
listEl.addEventListener('scroll', closeMenu);

async function run(fn) {
  try { await fn(); }
  catch (err) { statusEl.textContent = `Failed: ${err.message}`; }
}

// ── Click / double-click gestures on the thumbnail + name ──
function bindOpenGestures(el, src) {
  let timer = null;
  el.addEventListener('click', () => {
    if (timer) return;                      // second click of a dblclick
    timer = setTimeout(() => { timer = null; run(() => openEditorWithConfirm(src)); }, 220);
  });
  el.addEventListener('dblclick', () => {
    if (timer) {
      clearTimeout(timer);
      timer = null;
    }
    run(() => openCrop(src));
  });
}

// ── Actions ──
function download(src) {
  return chrome.downloads.download({ url: src, filename: filenameFromUrl(src) });
}

// Single-click open: confirm incognito vs normal, then open in an editor tab.
function openEditorWithConfirm(src) {
  const incognito = confirm(
    'Open this image in the Stencil editor?\n\nOK = incognito mode (won’t be saved)\nCancel = normal editor');
  return sendToEditor(src, incognito);
}

async function sendToEditor(src, incognito) {
  statusEl.textContent = 'Loading image…';
  const { page } = await getSettings();
  const dataUrl = await fetchAsDataUrl(src);
  await openEditorTab({ dataUrl, name: filenameFromUrl(src), page: { size: page }, incognito });
  window.close();
}

// Crop opens a small in-page modal on the current page (full editor stays a tab).
async function openCrop(src) {
  await launchCrop({ src, tabId: state.activeTabId });
  window.close();
}

// ── Hover preview ──
function bindPreview(el, src) {
  el.addEventListener('mouseenter', () => { previewImg.src = src; previewEl.hidden = false; position(); });
  el.addEventListener('mouseleave', () => { previewEl.hidden = true; });
  function position() {
    const r = el.getBoundingClientRect();
    const pw = previewEl.offsetWidth || 270, ph = previewEl.offsetHeight || 270;
    let x = r.right + 12; if (x + pw > window.innerWidth) x = Math.max(12, r.left - pw - 12);
    let y = r.top; if (y + ph > window.innerHeight) y = Math.max(12, window.innerHeight - ph - 12);
    previewEl.style.left = `${x}px`; previewEl.style.top = `${y}px`;
  }
}

// ── Wiring ──
let searchTimer = null;
document.getElementById('f-search').addEventListener('input', () => { clearTimeout(searchTimer); searchTimer = setTimeout(applyFilters, 150); });
['f-img', 'f-bg'].forEach(id => document.getElementById(id).addEventListener('change', applyFilters));
document.getElementById('f-fmt-toggle').addEventListener('click', () => {
  const target = !allFormatsChecked();
  formatCheckboxes().forEach(c => { c.checked = target; });
  updateToggleLabel();
  applyFilters();
});
['f-minw', 'f-maxw', 'f-minh', 'f-maxh'].forEach(id => document.getElementById(id).addEventListener('input', () => { clearTimeout(searchTimer); searchTimer = setTimeout(applyFilters, 150); }));
document.getElementById('rescan').addEventListener('click', scan);
document.getElementById('open-options').addEventListener('click', () => chrome.runtime.openOptionsPage());

scan();
