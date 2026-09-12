import { filenameFromUrl, setSettings } from '../lib/stencil.js';
import { loadPins, isPinnedIn, siteOf, setPinned } from '../lib/pins.js';
import { shortName } from '../lib/displayName.js';
import { sourceOf, pinnable } from '../lib/imageModel.js';
import { extractDraggedUrl, guessKindFromUrl } from '../lib/dragUrl.js';
import { entryFromUrl, sameSource } from '../lib/dropEntry.js';
import { URL_DRAG_TYPES } from '../lib/chatDrop.js';
import { flashLanding } from '../lib/motion.js';
import { listEl, statusEl, IS_SIDE_PANEL, IS_DEVTOOLS } from './panelDom.js';
import { state, rowElFor, rowResource } from './model.js';
import { applyFilters } from './filters.js';
import { annotatePinned } from './scan.js';

export const download = (src) => chrome.downloads.download({ url: src, filename: filenameFromUrl(src) });

// The storage write also reaches the other open panels and the page API via storage.onChanged.
export const setPinnedState = async (image, pinned) => {
  image.pinned = pinned;
  await setPinned({
    // A merged editor-mode scan spans several pages, so a pin keys on the row's OWN page.
    source: sourceOf(image), site: siteOf(rowResource(image)), resource: rowResource(image),
    name: image.name, kind: image.kind, pinned,
  });
  applyFilters();
  flashRow(image);
};

export const togglePin = async (image) => setPinnedState(image, !image.pinned);

const rowForDroppedUrl = (src, name) => ({ ...entryFromUrl(src, { name }), pinned: true });

// Drag-to-pin: a matching scanned row is reused; otherwise the URL is pinned as a fresh row.
const pinFromDroppedUrl = async (url) => {
  const src = String(url || '').trim();
  if (!src) return;
  const site = siteOf(state.activeUrl);
  if (!site) { statusEl.textContent = 'Can’t pin here — this page can’t be scanned.'; return; }
  const pinName = filenameFromUrl(src);
  const existing = state.all.find((im) => pinnable(im) && sameSource(sourceOf(im), src));
  if (existing) {
    // No message when already pinned — it read as a false "not pinned".
    if (existing.pinned) { flashRow(existing); return; }
    if (!state.showPinned) await enableShowPinned();
    await setPinnedState(existing, true);
    statusEl.textContent = `Pinned: ${shortName(existing.name)}`;
    return;
  }
  const pins = await loadPins();
  if (isPinnedIn(pins, site, src)) return;
  const row = rowForDroppedUrl(src, pinName);
  state.all.push(row);
  await setPinned({ source: src, site, resource: state.activeUrl, name: pinName, kind: guessKindFromUrl(src), pinned: true });
  // A silent storage failure must not read as "pinned".
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
  flashRow(row, { landing: true });
  statusEl.textContent = `Pinned: ${shortName(pinName)}`;
};

// Drag-to-pin would otherwise write a pin the user cannot see.
const enableShowPinned = async () => {
  state.showPinned = true;
  const cb = document.getElementById('f-show-pinned');
  if (cb) cb.checked = true;
  try { await setSettings({ showPinned: true }); } catch { /* setting won't persist */ }
};

export const flashRow = (image, { landing = false } = {}) => {
  const row = state.filtered.includes(image) ? rowElFor(image) : null;
  if (!row) return;
  row.scrollIntoView({ behavior: 'smooth', block: 'center' });
  flashLanding(row, landing ? 'just-dropped' : 'just-pinned', 900);
};

// Only the persistent surfaces: the popup closes on blur mid-drag.
if (IS_SIDE_PANEL || IS_DEVTOOLS) {
  // Toggle the class only on a CHANGE — dragover fires continuously and re-toggling
  // restarts the cue's paint.
  let dragOver = false;
  const setDrag = (on) => {
    if (!!on === dragOver) return;
    dragOver = !!on;
    listEl.classList.toggle('drag-over', dragOver);
  };
  // The Assistant section and the header's brand zone own their drops.
  const overAssistant = (e) => !!(e.target && e.target.closest
    && e.target.closest('#sec-assistant, header .logo, header h1'));
  // A pin keys on a URL, so 'Files' payloads do not qualify here.
  const isDropCandidate = (e) => {
    if (overAssistant(e)) return false;
    const t = e.dataTransfer && e.dataTransfer.types;
    if (!t || t.includes('application/x-stencil-drag')) return false;
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
