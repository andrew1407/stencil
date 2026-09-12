// Drag-and-drop onto the embedded Assistant section: classification + the drop cue.
import { extractDraggedUrl } from './dragUrl.js';

// Text payload types a draggable URL can ride in. The drag-to-pin listener (popup.js)
// takes these but NOT 'Files' — a local file has no URL a pin could key on.
export const URL_DRAG_TYPES = ['text/uri-list', 'text/html', 'text/plain', 'text/x-moz-url'];

export const DRAG_TYPES = ['Files', ...URL_DRAG_TYPES];

export const isDropCandidate = (types) => {
  const t = Array.isArray(types) ? types : (types ? [...types] : []);
  return DRAG_TYPES.some((x) => t.includes(x));
};

// Mirror of browser/js/core/videoFrame.js isVideoFile.
export const isVideoFile = (file) => {
  if (!file) return false;
  if (typeof file.type === 'string' && file.type.startsWith('video/')) return true;
  return /\.(mp4|webm|ogg|ogv|mov|m4v|mkv|avi)$/i.test(file.name || '');
};

// Local files win, else the first extractable URL; null when nothing is usable.
export const classifyDrop = (dt) => {
  if (!dt) return null;
  const files = dt.files ? [...dt.files] : [];
  if (files.length) return { kind: 'files', files };
  const url = extractDraggedUrl((t) => dt.getData(t));
  return url ? { kind: 'url', url } : null;
};

// A dragleave with a relatedTarget still inside root is an internal move and must NOT
// clear the drop cue; a null relatedTarget means the drag left the window.
export const leftTarget = (root, relatedTarget) => {
  if (!relatedTarget) return true;
  return typeof root.contains === 'function' ? !root.contains(relatedTarget) : false;
};

// Non-candidates are left to the browser; `onDrop(payload)` never sees null.
export const wireDropTarget = (root, { highlight, onDrop }) => {
  // dragover fires continuously, so the class is only touched on a CHANGE — re-toggling
  // it restarts CSS transitions and strobes the cue.
  let over = false;
  const setOver = (on) => {
    const next = !!on;
    if (next === over) return;
    over = next;
    highlight.classList.toggle('drop-over', next);
  };
  const accept = (e) => {
    if (!e.dataTransfer || !isDropCandidate(e.dataTransfer.types)) return;
    e.preventDefault();
    try { e.dataTransfer.dropEffect = 'copy'; } catch { /* locked dataTransfer */ }
    setOver(true);
  };
  root.addEventListener('dragenter', accept);
  root.addEventListener('dragover', accept);
  root.addEventListener('dragleave', (e) => { if (leftTarget(root, e.relatedTarget)) setOver(false); });
  // dragend bubbles from the drag SOURCE, so a drop elsewhere or an Escape clears the cue.
  root.addEventListener('dragend', () => setOver(false));
  root.addEventListener('drop', (e) => {
    const payload = classifyDrop(e.dataTransfer);
    setOver(false);
    if (!payload) return;
    e.preventDefault();
    onDrop(payload);
  });
  return { setOver, isOver: () => over };
};
