// Windows move by their header, the way the desktop's do. The offset is inline on the box
// and cleared on every open, so a window returns to where its flight puts it rather than
// remembering a nudge from an hour ago.

// Controls in the header (Close, and the chat's own chrome) keep their click.
const isGrabbable = (el) => el && !el.closest('button, input, select, textarea, a, [role="button"]');

// How much of the header stays reachable, so a window is never dragged out of reach.
const KEEP_PX = 48;

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

// `boxOf` is the shell's own accessor — the box is re-created by some windows.
export const wireModalDrag = (overlay, boxOf) => {
  if (!overlay?.addEventListener) return { reset: () => {} };
  let at = null;   // { id, startX, startY, dx, dy, box }
  let held = { dx: 0, dy: 0 };

  const paint = () => {
    const box = boxOf();
    if (!box?.style) return;
    box.style.transform = held.dx || held.dy ? `translate(${held.dx}px, ${held.dy}px)` : '';
  };
  const reset = () => { held = { dx: 0, dy: 0 }; at = null; paint(); };

  overlay.addEventListener('pointerdown', (e) => {
    const box = boxOf();
    const header = e.target?.closest?.('.settings-header');
    if (!box || !header || !box.contains(header) || !isGrabbable(e.target)) return;
    // The flight owns the transform while it plays; a grab mid-flight would fight it.
    if (overlay.classList.contains('modal-closing')) return;
    at = { id: e.pointerId, startX: e.clientX, startY: e.clientY, ...held,
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
    held = clampOffset(rest, { width: window.innerWidth, height: window.innerHeight },
                       want.dx, want.dy);
    paint();
  });

  const drop = (e) => {
    if (!at || (e && e.pointerId !== at.id)) return;
    boxOf()?.classList?.remove('modal-dragging');
    at = null;
  };
  overlay.addEventListener('pointerup', drop);
  overlay.addEventListener('pointercancel', drop);

  return { reset };
};
