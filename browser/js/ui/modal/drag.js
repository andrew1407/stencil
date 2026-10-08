// Windows move by their header, the way the desktop's do. The offset is the box's inline
// `translate`, which no entrance keyframe animates, cleared on every open, so a window returns to
// where its flight puts it rather than remembering a nudge from an hour ago.

// Controls in the header (Close, and the chat's own chrome) keep their click.
const isGrabbable = (el) => el && !el.closest('button, input, select, textarea, a, [role="button"]');

// How much of the header stays reachable, so a window is never dragged out of reach.
const KEEP_PX = 48;
// A window opened at a point stays this far inside the viewport (popoverPosition's margin).
const PLACE_MARGIN_PX = 8;

export const clampOffset = (box, viewport, dx, dy) => {
  if (!box || !viewport) return { dx: 0, dy: 0 };
  const minX = -(box.left) - box.width + KEEP_PX;
  const maxX = viewport.width - box.left - KEEP_PX;
  const minY = -box.top;                                   // never above the top edge
  const maxY = viewport.height - box.top - KEEP_PX;
  return {
    dx: Math.min(Math.max(dx, minX), maxX),
    dy: Math.min(Math.max(dy, minY), maxY),
  };
};

// The offset that puts the top-left corner of `box` (its rest rect) on `point`, the whole box inside
// the viewport: near the right or bottom edge it shifts back to fit; one bigger than the room keeps its
// top-left edge in view.
export const placeOffset = (box, point, viewport, margin = PLACE_MARGIN_PX) => {
  if (!box || !point || !viewport) return { dx: 0, dy: 0 };
  const axis = (start, size, at, room) =>
    Math.round(Math.max(margin - start, Math.min(at - start, room - margin - size - start)));
  return { dx: axis(box.left, box.width, point.x, viewport.width),
           dy: axis(box.top, box.height, point.y, viewport.height) };
};

// The resize keeps an edge put through the same `translate` (resize.js), so the offset is read off the box.
const offsetOf = (box) => {
  const [x, y] = String(box?.style?.translate || '').split(/\s+/).map(parseFloat);
  return { dx: x || 0, dy: y || 0 };
};
const paint = (box, { dx, dy }) => {
  if (box?.style) box.style.translate = dx || dy ? `${dx}px ${dy}px` : '';
};
const viewport = () => ({ width: window.innerWidth, height: window.innerHeight });

// `boxOf` is the shell's own accessor — the box is re-created by some windows.
export const wireModalDrag = (overlay, boxOf) => {
  if (!overlay?.addEventListener) return { reset: () => {}, placeAt: () => {} };
  let at = null;   // { id, startX, startY, dx, dy, box }

  const reset = () => { at = null; paint(boxOf(), { dx: 0, dy: 0 }); };
  // `opening`: the box is only now shown, so its entrance keyframes are held off while it is measured.
  const placeAt = (point, opening = false) => {
    const box = boxOf();
    if (!box?.getBoundingClientRect) return;
    const was = offsetOf(box);
    if (opening) overlay.classList.add('modal-measuring');
    const r = box.getBoundingClientRect();
    if (opening) overlay.classList.remove('modal-measuring');
    const rest = { left: r.left - was.dx, top: r.top - was.dy, width: r.width, height: r.height };
    paint(box, placeOffset(rest, point, viewport()));
  };

  overlay.addEventListener('pointerdown', (e) => {
    const box = boxOf();
    const header = e.target?.closest?.('.settings-header');
    if (!box || !header || !box.contains(header) || !isGrabbable(e.target)) return;
    // The flight owns the transform while it plays; a grab mid-flight would fight it.
    if (overlay.classList.contains('modal-closing')) return;
    at = { id: e.pointerId, startX: e.clientX, startY: e.clientY, ...offsetOf(box),
           box: box.getBoundingClientRect() };
    header.setPointerCapture?.(e.pointerId);
    box.classList.add('modal-dragging');
  });

  overlay.addEventListener('pointermove', (e) => {
    if (!at || e.pointerId !== at.id) return;
    const want = { dx: at.dx + (e.clientX - at.startX), dy: at.dy + (e.clientY - at.startY) };
    // Measured against the box's rest position, which is where the offset is applied from.
    const rest = { left: at.box.left - at.dx, top: at.box.top - at.dy,
                   width: at.box.width, height: at.box.height };
    paint(boxOf(), clampOffset(rest, viewport(), want.dx, want.dy));
  });

  const drop = (e) => {
    if (!at || (e && e.pointerId !== at.id)) return;
    boxOf()?.classList?.remove('modal-dragging');
    at = null;
  };
  overlay.addEventListener('pointerup', drop);
  overlay.addEventListener('pointercancel', drop);

  return { reset, placeAt };
};
