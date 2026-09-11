import { MSG } from '../lib/messages.js';
import { sourceOf, editableSrc } from '../lib/imageModel.js';
import { run } from './panelDom.js';
import { state, isOpened, surfaceTabId } from './model.js';
import { openHere, sendToEditor, openCrop } from './openActions.js';
import { openMenuAt } from './rowMenu.js';

// The row being dragged out of our own list, so an internal drag knows its exact entry
// (a `dragover` exposes the DataTransfer's TYPES but never its data).
let draggingRow = null;

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

export const bindRowGestures = (el, image) => {
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
export const bindRowDrag = (row, image) => {
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

// …read by the logo's drag menu, which only ever sees the payload's types.
export const getDraggingRow = () => draggingRow;
