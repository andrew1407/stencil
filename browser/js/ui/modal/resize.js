// Windows resize by their edges, the way the desktop's do. The size is inline on the box and
// cleared on every open, so a window opens at its own size rather than one stretched an hour
// ago. Desktop twin: the frame of any QDialog.
import { clamp } from '../../utils/math.js';

const EDGE_PX = 8;      // the band along each edge that takes the pointer
const MARGIN_PX = 20;   // stays this far inside the viewport, as .app-modal's max-width does
export const MIN_W = 320;
export const MIN_H = 200;

export const CURSORS = Object.freeze({
  n: 'ns-resize', s: 'ns-resize', e: 'ew-resize', w: 'ew-resize',
  ne: 'nesw-resize', sw: 'nesw-resize', nw: 'nwse-resize', se: 'nwse-resize',
});

// The edge a point touches — 'n', 'se', … — or '' inside the box or away from it.
export const edgeAt = (box, x, y, band = EDGE_PX) => {
  if (!box) return '';
  const right = box.left + box.width, bottom = box.top + box.height;
  if (x < box.left - band || x > right + band || y < box.top - band || y > bottom + band) return '';
  const v = y <= box.top + band ? 'n' : y >= bottom - band ? 's' : '';
  const h = x <= box.left + band ? 'w' : x >= right - band ? 'e' : '';
  return v + h;
};

// The rect after dragging edge `dir` by (dx, dy): the opposite edge stays put, the box never
// falls under the minimum nor past the viewport's margin.
export const resizeRect = (r, dir, dx, dy, viewport,
                           { minW = MIN_W, minH = MIN_H, margin = MARGIN_PX } = {}) => {
  let { left, top, width, height } = r;
  const right = left + width, bottom = top + height;
  if (dir.includes('e')) width = clamp(width + dx, minW, Math.max(minW, viewport.width - margin - left));
  if (dir.includes('s')) height = clamp(height + dy, minH, Math.max(minH, viewport.height - margin - top));
  if (dir.includes('w')) { width = clamp(width - dx, minW, Math.max(minW, right - margin)); left = right - width; }
  if (dir.includes('n')) { height = clamp(height - dy, minH, Math.max(minH, bottom - margin)); top = bottom - height; }
  return { left: Math.round(left), top: Math.round(top), width: Math.round(width), height: Math.round(height) };
};

// `boxOf` is the shell's own accessor — the box is re-created by some windows. A popover has
// its place and cap from its opener, so it keeps them.
export const wireModalResize = (overlay, boxOf) => {
  if (!overlay?.addEventListener) return { reset: () => {} };
  let at = null;   // { id, dir, startX, startY, rect }
  const viewport = () => ({ width: window.innerWidth, height: window.innerHeight });
  const active = () => !overlay.classList.contains('modal-popover') && !overlay.classList.contains('modal-closing');

  const showEdge = (box, dir) => {
    if (!box?.style) return;
    if (dir) {
      box.dataset.edge = dir;
      box.style.setProperty('--edge-cursor', CURSORS[dir]);
    } else {
      delete box.dataset.edge;
      box.style.removeProperty('--edge-cursor');
    }
  };
  // The box sits centred by its overlay, so a new width would move both edges: the held
  // size is set first, then `translate` carries the box back onto the edge that must not move.
  const place = (box, want) => {
    box.style.minHeight = '0';
    box.style.maxHeight = 'none';
    box.style.width = `${want.width}px`;
    box.style.height = `${want.height}px`;
    box.style.translate = '';
    const now = box.getBoundingClientRect();
    box.style.translate = `${want.left - now.left}px ${want.top - now.top}px`;
  };
  const reset = () => {
    at = null;
    const box = boxOf();
    if (!box?.style) return;
    for (const p of ['width', 'height', 'minHeight', 'maxHeight', 'translate']) box.style[p] = '';
    box.classList.remove('modal-resizing');
    showEdge(box, '');
  };

  // Capture, ahead of the header's drag and the box's own controls: the band is the frame's.
  overlay.addEventListener('pointerdown', (e) => {
    const box = boxOf();
    if (!box || !active()) return;
    const dir = edgeAt(box.getBoundingClientRect(), e.clientX, e.clientY);
    if (!dir) return;
    e.preventDefault();
    e.stopPropagation();
    at = { id: e.pointerId, dir, startX: e.clientX, startY: e.clientY, rect: box.getBoundingClientRect() };
    box.setPointerCapture?.(e.pointerId);
    box.classList.add('modal-resizing');
  }, true);

  overlay.addEventListener('pointermove', (e) => {
    const box = boxOf();
    if (!box) return;
    if (!at) {
      showEdge(box, active() ? edgeAt(box.getBoundingClientRect(), e.clientX, e.clientY) : '');
      return;
    }
    if (e.pointerId !== at.id) return;
    place(box, resizeRect(at.rect, at.dir, e.clientX - at.startX, e.clientY - at.startY, viewport()));
  });

  const drop = (e) => {
    if (!at || (e && e.pointerId !== at.id)) return;
    boxOf()?.classList?.remove('modal-resizing');
    at = null;
  };
  overlay.addEventListener('pointerup', drop);
  overlay.addEventListener('pointercancel', drop);
  overlay.addEventListener('pointerleave', () => { if (!at) showEdge(boxOf(), ''); });

  return { reset };
};
