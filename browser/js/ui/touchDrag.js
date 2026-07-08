// Pointer-based drag for the reorderable modal lists (projects + connections), because HTML5
// drag-and-drop never fires on TOUCH devices. Mouse keeps using native HTML5 DnD (the row stays
// draggable=true); this layer engages only for touch/pen (`pointerType !== 'mouse'`).
//
// UX: long-press a row to pick it up (a quick swipe just scrolls the list); then drag — the caller
// decides the drop target (reorder row vs a drag-out zone) via callbacks. The engine owns the
// floating translucent ghost (a clone of the row that follows the finger) and, after a real drag,
// swallows the trailing click so the drop doesn't also fire the row's tap-to-open.
//
// Rows must set `touch-action: pan-y` (CSS) so the list still scrolls vertically by touch; the
// long-press gate distinguishes a hold-to-drag from a swipe-to-scroll.
export function makeTouchDraggable(row, opts) {
  const {
    canStart,             // (e) => bool — false ignores this pointerdown (interactive child)
    onStart,              // () => void — drag began (after long-press)
    onMove,               // (clientX, clientY) => void
    onDrop,               // (clientX, clientY) => void
    onCancel,             // () => void — pointercancel while dragging
    longPressMs = 280,
    moveCancelPx = 12,    // finger travel (pre-drag) that reads as a scroll → abandon
  } = opts;

  let sx = 0, sy = 0, dxOff = 0, dyOff = 0, pid = null, timer = null, dragging = false, ghost = null;

  const makeGhost = () => {
    const g = row.cloneNode(true);
    const r = row.getBoundingClientRect();
    Object.assign(g.style, {
      position: 'fixed', left: `${r.left}px`, top: `${r.top}px`, width: `${r.width}px`,
      margin: '0', opacity: '0.6', pointerEvents: 'none', zIndex: '100005',
      transform: 'scale(1.02)', boxShadow: '0 8px 24px rgba(0,0,0,0.4)',
    });
    document.body.appendChild(g);
    return g;
  };
  const moveGhost = (x, y) => { if (ghost) { ghost.style.left = `${x - dxOff}px`; ghost.style.top = `${y - dyOff}px`; } };

  const cleanup = () => {
    if (ghost) { ghost.remove(); ghost = null; }
    if (pid != null) { try { row.releasePointerCapture(pid); } catch { /* not captured */ } }
    clearTimeout(timer); timer = null; dragging = false; pid = null;
  };

  const onDown = (e) => {
    if (e.pointerType === 'mouse') return;            // mouse → native HTML5 DnD path
    if (canStart && !canStart(e)) return;             // don't hijack checkbox / ⋯ / rename
    sx = e.clientX; sy = e.clientY; pid = e.pointerId;
    const r = row.getBoundingClientRect();
    dxOff = sx - r.left; dyOff = sy - r.top;
    timer = setTimeout(() => {
      dragging = true;
      try { row.setPointerCapture(pid); } catch { /* older UA */ }
      ghost = makeGhost();
      moveGhost(sx, sy);
      onStart && onStart();
    }, longPressMs);
  };
  const onMoveEv = (e) => {
    if (e.pointerId !== pid) return;
    if (!dragging) {                                   // still deciding hold-vs-swipe
      if (Math.hypot(e.clientX - sx, e.clientY - sy) > moveCancelPx) { clearTimeout(timer); pid = null; }
      return;
    }
    e.preventDefault();                                // suppress scroll while dragging
    moveGhost(e.clientX, e.clientY);
    onMove && onMove(e.clientX, e.clientY);
  };
  const finish = (e, cancelled) => {
    if (e.pointerId !== pid) return;
    const wasDragging = dragging;
    const x = e.clientX, y = e.clientY;
    cleanup();
    if (!wasDragging) return;
    // Swallow the click the browser synthesizes after this pointer sequence so a drag-drop
    // doesn't also trigger the row's tap-to-open.
    const swallow = (ev) => { ev.stopPropagation(); ev.preventDefault(); };
    row.addEventListener('click', swallow, { capture: true, once: true });
    setTimeout(() => row.removeEventListener('click', swallow, true), 500);
    if (cancelled) onCancel && onCancel(); else onDrop && onDrop(x, y);
  };

  row.addEventListener('pointerdown', onDown);
  row.addEventListener('pointermove', onMoveEv, { passive: false });
  row.addEventListener('pointerup', (e) => finish(e, false));
  row.addEventListener('pointercancel', (e) => finish(e, true));
}
