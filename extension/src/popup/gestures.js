import { MSG } from '../lib/messages.js';
import { sourceOf, editableSrc } from '../lib/imageModel.js';
import { run } from './panelDom.js';
import { state, isOpened, surfaceTabId } from './model.js';
import { openHere, sendToEditor, openCrop } from './openActions.js';
import { openMenuAt } from './rowMenu.js';

// A `dragover` exposes the DataTransfer's TYPES but never its data, so the entry is kept here.
let draggingRow = null;

// Click and double-click are disambiguated with a short timer.
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

const openRow = (image, el) => (state.mode === 'editor' ? openHere(image, false, undefined, el) : sendToEditor(image, false));

const bindOpenGestures = (el, image) =>
  bindGestures(el, () => openRow(image, el), () => openCrop(image));

export const bindRowGestures = (el, image) => {
  // An already-opened image surfaces the resume / add-a-copy chooser instead of a new editor.
  if (isOpened(image)) {
    bindGestures(el,
      () => { const r = el.getBoundingClientRect(); openMenuAt(image, r.left, r.bottom); },
      () => openCrop(image));
    return;
  }
  if (image.kind !== 'video') return bindOpenGestures(el, image);
  // A video with no frame and no poster opens its media in a tab.
  const es = editableSrc(image);
  const onClick = es
    ? () => openRow(image, el)
    : (image.videoUrl ? () => chrome.tabs.create({ url: image.videoUrl }) : null);
  const onDouble = es ? () => openCrop(image) : null;
  bindGestures(el, onClick, onDouble);
};

// Mirrors browser/js/ui/dragGhost.js: Chrome snapshots the row before any .dragging style
// applies, so a clone carries the faded look. A bare row's transparent background would
// read as nothing, hence the solid card.
const setTranslucentDragImage = (e, row) => {
  try {
    const ghost = row.cloneNode(true);
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

// Drag a row OUT of the panel: onto the page's drop overlay, into any app, or the editor tab.
export const bindRowDrag = (row, image) => {
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
      // Our own drag type, so the panel's drag-IN handler ignores a row dropped back inside.
      dt.setData('application/x-stencil-drag', image.kind || 'img');
    } catch { /* some contexts lock dataTransfer — the drag still starts */ }
    setTranslucentDragImage(e, row);
    row.classList.add('dragging');
    draggingRow = image;
    // The overlay arms on the tab in FRONT of the user: in editor mode the editor, not the listed page.
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

export const getDraggingRow = () => draggingRow;
