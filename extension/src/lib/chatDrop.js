// ── Assistant-section drag-and-drop (pure, unit-testable) ───────────────────
// Classifies what landed on the embedded Assistant section and drives the
// drop-highlight state. URL extraction REUSES dragUrl.js extractDraggedUrl — the
// extension's existing cross-page drag parsing. No DOM access at module level:
// wireDropTarget only touches the elements it is handed, so `node --test` can
// drive it with stubs.
import { extractDraggedUrl } from './dragUrl.js';

// Text payload types a draggable URL can ride in. Shared with the drag-to-pin
// document listener (popup.js), which accepts these but deliberately NOT 'Files' —
// a local file has no URL a pin could key on.
export const URL_DRAG_TYPES = ['text/uri-list', 'text/html', 'text/plain', 'text/x-moz-url'];

// Drag payload types the assistant accepts (files, or any text payload a URL can ride in).
export const DRAG_TYPES = ['Files', ...URL_DRAG_TYPES];

// Is this drag something the assistant can take? `types` is DataTransfer.types.
export const isDropCandidate = (types) => {
  const t = Array.isArray(types) ? types : (types ? [...types] : []);
  return DRAG_TYPES.some((x) => t.includes(x));
};

// Is this File a video (by MIME, falling back to a common video extension)?
// Mirror of browser/js/core/videoFrame.js isVideoFile.
export const isVideoFile = (file) => {
  if (!file) return false;
  if (typeof file.type === 'string' && file.type.startsWith('video/')) return true;
  return /\.(mp4|webm|ogg|ogv|mov|m4v|mkv|avi)$/i.test(file.name || '');
};

// Classify a drop's DataTransfer: local files win, else the first extractable URL.
// Returns { kind: 'files', files } | { kind: 'url', url } | null (nothing usable).
export const classifyDrop = (dt) => {
  if (!dt) return null;
  const files = dt.files ? [...dt.files] : [];
  if (files.length) return { kind: 'files', files };
  const url = extractDraggedUrl((t) => dt.getData(t));
  return url ? { kind: 'url', url } : null;
};

// Did the drag actually LEAVE `root`, or just move between its children? A dragleave
// with a relatedTarget still inside root is an internal move and must NOT clear the
// drop cue (it would flicker as the cursor crosses the transcript's messages).
// relatedTarget === null means the drag left the window entirely.
export const leftTarget = (root, relatedTarget) => {
  if (!relatedTarget) return true;
  return typeof root.contains === 'function' ? !root.contains(relatedTarget) : false;
};

// Wire `root` (the assistant section) as a drop target: candidates get preventDefault +
// the `drop-over` class on `highlight` while hovering; a drop is classified and
// handed to `onDrop(payload)` (payload never null). Non-candidates are left to the
// browser. Returns { setOver, isOver } for cleanup/tests.
export const wireDropTarget = (root, { highlight, onDrop }) => {
  // dragover fires continuously (every few ms) while the cursor sits over the target,
  // so the class is only touched on a CHANGE — re-toggling it restarted CSS
  // transitions and made the cue strobe under the cursor.
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
  // A drag that ends anywhere (dropped elsewhere, or cancelled with Escape) must not
  // leave the cue painted — dragend bubbles to the document from the drag SOURCE.
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
