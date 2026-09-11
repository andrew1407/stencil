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

// ── Actions ──
export const download = (src) => chrome.downloads.download({ url: src, filename: filenameFromUrl(src) });

// Pin / unpin an image on this site, then re-render so it floats (or settles back).
// The storage write also reaches any open side panel / DevTools panel and the page API
// (entry.pinned) via their storage.onChanged listeners.
export const setPinnedState = async (image, pinned) => {
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
export const togglePin = async (image) => setPinnedState(image, !image.pinned);

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
export const flashRow = (image, { landing = false } = {}) => {
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
